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
TARGET_ADDED_BYTES = 40 * MIB
HARD_STOP_ADDED_BYTES = 60 * MIB
SYSTEM_PREFIXES = ("/System/Library/", "/usr/lib/")
FORBIDDEN_PREFIXES = ("/opt/homebrew", "/usr/local")
SCIENCE_DEPENDENCY_PATTERN = re.compile(
    r"(?:^|[/_.-])(?:lib)?(?:gdal|proj|zstd)(?:$|[/_.-])", re.IGNORECASE)
SCIENCE_SYMBOL_PATTERN = re.compile(
    r"(?:^|\s)_?(?:GDAL[A-Z_]|OGR[A-Z_]|OSR[A-Z_]|proj_[a-z]|ZSTD_[A-Z])")
SCIENCE_STRING_PATTERN = re.compile(
    r"(?:GDALAllRegister|GDALOpen(?:Ex)?|GDAL_DATA|PROJ_LIB|"
    r"proj_(?:context_create|create_crs_to_crs)|"
    r"ZSTD_(?:compress|decompress|createDStream))")


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

    def symbols(self, path):
        return self._run(["nm", "-a", str(path)]).splitlines()


def bundle_size(path):
    return sum(
        item.stat().st_size for item in Path(path).rglob("*")
        if item.is_file() and not item.is_symlink())


def classify_size(size_bytes):
    if size_bytes <= TARGET_ADDED_BYTES:
        return "PASS"
    if size_bytes <= HARD_STOP_ADDED_BYTES:
        return "REVIEW_REQUIRED"
    return "STOP"


def evaluate_size_gates(delta_bytes, science_closure_bytes):
    size_gates = {
        "delta": classify_size(delta_bytes),
        "science_closure": classify_size(science_closure_bytes),
    }
    review_items = []
    violations = []
    for label, value in (("added bundle size", delta_bytes),
                         ("science closure size", science_closure_bytes)):
        gate = classify_size(value)
        if gate == "REVIEW_REQUIRED":
            review_items.append(
                f"{label} {value} exceeds {TARGET_ADDED_BYTES} and requires review")
        elif gate == "STOP":
            violations.append(
                f"{label} {value} exceeds immutable stop {HARD_STOP_ADDED_BYTES}")
    if violations:
        status = "STOP"
    elif review_items:
        status = "REVIEW_REQUIRED"
    else:
        status = "PASS"
    return {
        "status": status,
        "exit_code": {"PASS": 0, "STOP": 1, "REVIEW_REQUIRED": 2}[status],
        "size_gates": size_gates,
        "review_items": sorted(review_items),
        "violations": sorted(violations),
    }


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


def expand_runpaths(values, declarer, executable):
    return [str(expand_loader_token(value, declarer, executable).resolve())
            for value in values]


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
        for rpath in rpaths:
            candidate = Path(rpath) / suffix
            if candidate.exists():
                return candidate.resolve()
        return None
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
    violations = []
    unresolved = []
    metadata = {}
    for relative, binary in sorted(binary_by_relative.items()):
        dependencies = inspector.dependencies(binary)
        rpaths = inspector.rpaths(binary)
        strings = inspector.string_references(binary)
        symbols = inspector.symbols(binary)
        metadata[relative] = {
            "path": binary,
            "dependencies": dependencies,
            "rpaths": rpaths,
            "expanded_rpaths": expand_runpaths(rpaths, binary, executable),
            "strings": strings,
            "symbols": symbols,
        }

    science_nodes = [
        relative for relative in metadata
        if Path(relative).name == science_plugin_name]
    science_relative = science_nodes[0] if len(science_nodes) == 1 else None
    if len(science_nodes) != 1:
        violations.append(
            f"expected exactly one {science_plugin_name}, found {len(science_nodes)}")

    for relative, item in sorted(metadata.items()):
        exempt = relative == science_relative
        for raw in item["dependencies"]:
            forbidden = is_forbidden_reference(raw, source_roots)
            if forbidden:
                violations.append(f"{forbidden} runtime reference in {relative}: {raw}")
            if SCIENCE_DEPENDENCY_PATTERN.search(Path(raw).name) and not exempt:
                violations.append(
                    f"dynamic science dependency in {relative}: {raw}")
        for rpath in item["rpaths"]:
            forbidden = is_forbidden_reference(rpath, source_roots)
            if forbidden:
                violations.append(f"{forbidden} rpath in {relative}: {rpath}")
        for value in item["strings"]:
            forbidden = is_forbidden_reference(value, source_roots)
            if forbidden:
                violations.append(f"{forbidden} string in {relative}: {value}")
            if SCIENCE_STRING_PATTERN.search(value) and not exempt:
                violations.append(f"static science string in {relative}: {value}")
        for value in item["symbols"]:
            if SCIENCE_SYMBOL_PATTERN.search(value) and not exempt:
                violations.append(f"static science symbol in {relative}: {value}")

    graph = {}
    visited_contexts = set()

    def traverse(relative, inherited_rpaths):
        context = (relative, tuple(inherited_rpaths))
        if context in visited_contexts:
            return
        visited_contexts.add(context)
        item = metadata[relative]
        search_rpaths = list(dict.fromkeys(
            item["expanded_rpaths"] + inherited_rpaths))
        edges = []
        for raw in item["dependencies"]:
            if raw.startswith(SYSTEM_PREFIXES):
                edges.append({
                    "dependency": raw, "resolved": None,
                    "external": None, "system": True})
                continue
            resolved_path = resolve_dependency(
                raw, item["path"], executable, app, search_rpaths)
            resolved_relative = (
                internal_relative(resolved_path, app) if resolved_path else None)
            external = (
                str(resolved_path) if resolved_path and resolved_relative is None else None)
            if resolved_path is None:
                message = f"unresolved dependency in {relative}: {raw}"
                unresolved.append(message)
                violations.append(message)
            elif external:
                violations.append(
                    f"external dependency in {relative}: {raw} -> {external}")
            edge = {
                "dependency": raw,
                "resolved": resolved_relative,
                "external": external,
                "system": False,
            }
            edges.append(edge)
            if resolved_relative in metadata:
                traverse(resolved_relative, search_rpaths)
        graph.setdefault(relative, edges)

    traverse(main_relative, [])
    main_runpaths = metadata[main_relative]["expanded_rpaths"]
    for relative in sorted(metadata):
        if relative != main_relative:
            traverse(relative, main_runpaths)

    _, main_science_violations = reachable_nodes(
        graph, main_relative, science_plugin_name)
    violations.extend(main_science_violations)
    if science_relative is None:
        science_reachable = set(science_nodes)
    else:
        science_reachable, _ = reachable_nodes(
            graph, science_relative, science_plugin_name)
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
    size_result = evaluate_size_gates(delta_bytes, science_closure_bytes)
    violations.extend(size_result["violations"])
    unresolved = sorted(set(unresolved))
    violations = sorted(set(violations))
    if violations:
        status = "STOP"
    else:
        status = size_result["status"]
    return {
        "schema_version": 2,
        "ok": status == "PASS",
        "status": status,
        "exit_code": {"PASS": 0, "STOP": 1, "REVIEW_REQUIRED": 2}[status],
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
        "size_gates": size_result["size_gates"],
        "thresholds": {
            "target_added_bytes": TARGET_ADDED_BYTES,
            "hard_stop_added_bytes": HARD_STOP_ADDED_BYTES,
        },
        "review_items": size_result["review_items"],
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
        "",
        "Size gates",
        f"  target: <= {result['thresholds']['target_added_bytes']} bytes",
        f"  review: > {result['thresholds']['target_added_bytes']} and "
        f"<= {result['thresholds']['hard_stop_added_bytes']} bytes",
        f"  immutable stop: > {result['thresholds']['hard_stop_added_bytes']} bytes",
        f"  delta: {result['size_gates']['delta']}",
        f"  science closure: {result['size_gates']['science_closure']}",
        "",
        "Dependency graph",
    ]
    for node, edges in sorted(result["graph"].items()):
        lines.append(f"  {node}")
        for edge in edges:
            destination = edge["resolved"] or edge.get("external") or (
                "SYSTEM" if edge["system"] else "UNRESOLVED")
            lines.append(f"    {edge['dependency']} -> {destination}")
    lines.extend(["", "Science-only closure"])
    lines.extend(f"  {item}" for item in result["science_only_closure"])
    lines.extend(["", "Review items"])
    lines.extend(f"  {item}" for item in result["review_items"])
    if not result["review_items"]:
        lines.append("  none")
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
    )
    json_path = Path(arguments.json)
    text_path = Path(arguments.text) if arguments.text else json_path.with_suffix(".txt")
    json_path.parent.mkdir(parents=True, exist_ok=True)
    text_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    report = render_text(result)
    text_path.write_text(report)
    sys.stdout.write(report)
    return result["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
