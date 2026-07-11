#!/usr/bin/env python3
"""Recursively audit a macOS app dependency graph and ScienceEarth bundle cost."""

import argparse
import json
import plistlib
import re
import subprocess
import sys
from pathlib import Path


MIB = 1024 * 1024
SYSTEM_PREFIXES = ("/System/Library/", "/usr/lib/")
FORBIDDEN_PREFIXES = ("/opt/homebrew", "/usr/local")
SCIENCE_DEPENDENCY_PATTERN = re.compile(
    r"(?:^|[/_.-])(?:lib)?(?:gdal|proj|zstd)(?:$|[/_.-])", re.IGNORECASE)


class CommandInspector:
    """Read Mach-O metadata using the macOS command-line tools."""

    def _run(self, command):
        result = subprocess.run(
            command, check=True, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        return result.stdout

    def is_macho(self, path):
        return "Mach-O" in self._run(["file", "-b", str(path)])

    def dependencies(self, path):
        lines = self._run(["otool", "-L", str(path)]).splitlines()[1:]
        return [line.strip().split(" (", 1)[0] for line in lines if line.strip()]

    def rpaths(self, path):
        lines = self._run(["otool", "-l", str(path)]).splitlines()
        values = []
        for index, line in enumerate(lines):
            if line.strip() != "cmd LC_RPATH":
                continue
            for candidate in lines[index + 1:index + 5]:
                match = re.match(r"\s*path (.+) \(offset \d+\)", candidate)
                if match:
                    values.append(match.group(1))
                    break
        return values

    def string_references(self, path):
        return self._run(["strings", "-a", str(path)]).splitlines()


def bundle_size(path):
    return sum(
        item.stat().st_size for item in Path(path).rglob("*")
        if item.is_file() and not item.is_symlink())


def infer_main_relative(app):
    plist_path = Path(app) / "Contents" / "Info.plist"
    if plist_path.is_file():
        with plist_path.open("rb") as stream:
            executable = plistlib.load(stream).get("CFBundleExecutable")
        if executable:
            return str(Path("Contents") / "MacOS" / executable)
    candidates = sorted((Path(app) / "Contents" / "MacOS").glob("*"))
    if len(candidates) != 1:
        raise ValueError("cannot infer a unique main bundle executable")
    return str(candidates[0].relative_to(app))


def expand_loader_token(value, loader, executable):
    if value == "@loader_path":
        return loader.parent
    if value.startswith("@loader_path/"):
        return loader.parent / value[len("@loader_path/"):]
    if value == "@executable_path":
        return executable.parent
    if value.startswith("@executable_path/"):
        return executable.parent / value[len("@executable_path/"):]
    return Path(value)


def internal_relative(path, app):
    try:
        return str(path.resolve().relative_to(app.resolve()))
    except ValueError:
        return None


def resolve_dependency(raw, loader, executable, app, rpaths):
    if raw.startswith("@loader_path") or raw.startswith("@executable_path"):
        candidate = expand_loader_token(raw, loader, executable)
        return candidate.resolve() if candidate.exists() else None
    if raw.startswith("@rpath/"):
        suffix = raw[len("@rpath/"):]
        first_external = None
        for rpath in rpaths:
            root = expand_loader_token(rpath, loader, executable)
            candidate = root / suffix
            if candidate.exists():
                resolved = candidate.resolve()
                if internal_relative(resolved, app) is not None:
                    return resolved
                if first_external is None:
                    first_external = resolved
        return first_external
    if raw.startswith("/"):
        candidate = Path(raw)
        return candidate.resolve() if candidate.exists() else None
    return None


def is_forbidden_reference(value, source_roots):
    if any(prefix in value for prefix in FORBIDDEN_PREFIXES):
        return "forbidden prefix"
    normalized_roots = [str(Path(root).resolve()) for root in source_roots]
    if any(root in value for root in normalized_roots):
        return "source/build"
    if value.startswith("/") and (
            "/build/" in value or "/CMakeFiles/" in value or "/.worktrees/" in value):
        return "source/build"
    return None


def discover_macho(app, inspector):
    return sorted(
        (path.resolve() for path in Path(app).rglob("*")
         if path.is_file() and not path.is_symlink() and inspector.is_macho(path)),
        key=str,
    )


def reachable_nodes(graph, start, science_plugin_name=None):
    pending = [(start, False)]
    visited = set()
    reached = set()
    violations = []
    while pending:
        node, below_science = pending.pop()
        state = (node, below_science)
        if state in visited:
            continue
        visited.add(state)
        reached.add(node)
        below_science = below_science or Path(node).name == science_plugin_name
        if SCIENCE_DEPENDENCY_PATTERN.search(Path(node).name) and not below_science:
            violations.append(
                f"science dependency reachable from main outside {science_plugin_name}: {node}")
        for edge in graph.get(node, []):
            resolved = edge.get("resolved")
            if resolved:
                pending.append((resolved, below_science))
    return reached, violations


def audit_bundle(app, baseline, inspector=None, source_roots=None,
                 max_added_bytes=40 * MIB,
                 main_relative=None,
                 science_plugin_name="osgdb_science.so"):
    app = Path(app).resolve()
    baseline = Path(baseline).resolve()
    inspector = inspector or CommandInspector()
    source_roots = list(source_roots or [])
    if not app.is_dir() or not baseline.is_dir():
        raise ValueError("app and baseline must both be existing directories")
    main_relative = main_relative or infer_main_relative(app)
    executable = (app / main_relative).resolve()
    if not executable.is_file():
        raise ValueError(f"main executable is missing: {main_relative}")

    binaries = discover_macho(app, inspector)
    binary_by_relative = {internal_relative(path, app): path for path in binaries}
    if main_relative not in binary_by_relative:
        raise ValueError("main executable is not a Mach-O file")
    main_rpaths = inspector.rpaths(executable)
    graph = {}
    violations = []
    unresolved = []
    for relative, binary in sorted(binary_by_relative.items()):
        binary_rpaths = inspector.rpaths(binary)
        search_rpaths = list(dict.fromkeys(binary_rpaths + main_rpaths))
        edges = []
        for raw in inspector.dependencies(binary):
            forbidden = is_forbidden_reference(raw, source_roots)
            if forbidden:
                violations.append(f"{forbidden} runtime reference in {relative}: {raw}")
            if raw.startswith(SYSTEM_PREFIXES):
                edges.append({"dependency": raw, "resolved": None, "system": True})
                continue
            resolved_path = resolve_dependency(
                raw, binary, executable, app, search_rpaths)
            resolved_relative = (
                internal_relative(resolved_path, app) if resolved_path else None)
            if resolved_relative is None:
                message = f"unresolved dependency in {relative}: {raw}"
                unresolved.append(message)
                violations.append(message)
            edges.append({
                "dependency": raw,
                "resolved": resolved_relative,
                "system": False,
            })
        for rpath in binary_rpaths:
            forbidden = is_forbidden_reference(rpath, source_roots)
            if forbidden:
                violations.append(f"{forbidden} rpath in {relative}: {rpath}")
        for value in inspector.string_references(binary):
            forbidden = is_forbidden_reference(value, source_roots)
            if forbidden:
                violations.append(f"{forbidden} string in {relative}: {value}")
        graph[relative] = edges

    _, main_science_violations = reachable_nodes(
        graph, main_relative, science_plugin_name)
    violations.extend(main_science_violations)
    science_nodes = [
        relative for relative in graph if Path(relative).name == science_plugin_name]
    if len(science_nodes) != 1:
        violations.append(
            f"expected exactly one {science_plugin_name}, found {len(science_nodes)}")
        science_reachable = set(science_nodes)
    else:
        science_reachable, _ = reachable_nodes(
            graph, science_nodes[0], science_plugin_name)
    baseline_relatives = {
        str(path.relative_to(baseline)) for path in baseline.rglob("*")
        if path.is_file() or path.is_symlink()
    }
    science_only = sorted(science_reachable - baseline_relatives)
    science_closure_bytes = sum(
        (app / relative).stat().st_size for relative in science_only
        if (app / relative).is_file())

    baseline_bytes = bundle_size(baseline)
    total_bytes = bundle_size(app)
    delta_bytes = total_bytes - baseline_bytes
    if delta_bytes > max_added_bytes:
        violations.append(
            f"added bundle size {delta_bytes} exceeds limit {max_added_bytes}")
    if science_closure_bytes > max_added_bytes:
        violations.append(
            f"science closure size {science_closure_bytes} exceeds limit {max_added_bytes}")
    violations = sorted(set(violations))
    return {
        "schema_version": 1,
        "ok": not violations,
        "status": "PASS" if not violations else "STOP",
        "app": str(app),
        "baseline": str(baseline),
        "main": main_relative,
        "graph": graph,
        "unresolved": unresolved,
        "science_only_closure": science_only,
        "sizes": {
            "baseline_bytes": baseline_bytes,
            "total_bytes": total_bytes,
            "delta_bytes": delta_bytes,
            "science_closure_bytes": science_closure_bytes,
        },
        "thresholds": {"max_added_bytes": max_added_bytes},
        "violations": violations,
    }


def render_text(result):
    lines = [
        f"ScienceEarth macOS bundle audit: {result['status']}",
        f"App: {result['app']}",
        f"Baseline: {result['baseline']}",
        "",
        "Sizes",
        f"  baseline: {result['sizes']['baseline_bytes']} bytes",
        f"  total: {result['sizes']['total_bytes']} bytes",
        f"  delta: {result['sizes']['delta_bytes']} bytes",
        f"  science closure: {result['sizes']['science_closure_bytes']} bytes",
        f"  added-size limit: {result['thresholds']['max_added_bytes']} bytes",
        "",
        "Dependency graph",
    ]
    for node, edges in sorted(result["graph"].items()):
        lines.append(f"  {node}")
        for edge in edges:
            destination = edge["resolved"] or (
                "SYSTEM" if edge["system"] else "UNRESOLVED")
            lines.append(f"    {edge['dependency']} -> {destination}")
    lines.extend(["", "Science-only closure"])
    lines.extend(f"  {item}" for item in result["science_only_closure"])
    lines.extend(["", "Violations"])
    lines.extend(f"  {item}" for item in result["violations"])
    if not result["violations"]:
        lines.append("  none")
    return "\n".join(lines) + "\n"


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--json", required=True)
    parser.add_argument("--text")
    parser.add_argument("--max-added-bytes", type=int, default=40 * MIB)
    parser.add_argument("--source-root", action="append", default=[])
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    app = Path(arguments.app).resolve()
    baseline = Path(arguments.baseline).resolve()
    source_roots = arguments.source_root or [Path(__file__).resolve().parents[1]]
    result = audit_bundle(
        app=app,
        baseline=baseline,
        source_roots=source_roots,
        max_added_bytes=arguments.max_added_bytes,
    )
    json_path = Path(arguments.json)
    text_path = Path(arguments.text) if arguments.text else json_path.with_suffix(".txt")
    json_path.parent.mkdir(parents=True, exist_ok=True)
    text_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    report = render_text(result)
    text_path.write_text(report)
    sys.stdout.write(report)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
