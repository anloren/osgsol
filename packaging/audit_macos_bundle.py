#!/usr/bin/env python3
"""Recursively audit a macOS app dependency graph and ScienceEarth bundle cost."""

import argparse
import hashlib
import importlib.util
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
    r"(?:^|\s)_?(?:GDAL[A-Z_]|OGR[A-Z_]|OSR[A-Z_]|proj_[a-z]|ZSTD_[A-Za-z])")
SCIENCE_STRING_PATTERN = re.compile(
    r"(?:GDALAllRegister|GDALOpen(?:Ex)?|GDAL_DATA|PROJ_LIB|"
    r"proj_(?:context_create|create_crs_to_crs)|"
    r"ZSTD_(?:compress|decompress|createDStream))")


FINDING_SCHEMA_VERSION = 1


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MANIFEST = load_module(
    "scienceearth_g0_manifest_for_audit",
    Path(__file__).resolve().parent / "scienceearth" / "g0_manifest.py")


def normalize_subject(subject, source_roots):
    value = str(subject)
    roots = sorted((str(Path(root).resolve()) for root in source_roots),
                   key=len, reverse=True)
    for root in roots:
        value = value.replace(root, "${SOURCE_ROOT}")
    return value


def finding_identity(owner, category, normalized_subject):
    payload = json.dumps({
        "owner": owner,
        "category": category,
        "subject": normalized_subject,
    }, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def make_finding(owner, category, subject, source_roots):
    normalized = normalize_subject(subject, source_roots)
    return {
        "schema_version": FINDING_SCHEMA_VERSION,
        "owner": owner,
        "category": category,
        "subject": normalized,
        "identity": finding_identity(owner, category, normalized),
    }


def compare_identity_sets(candidate, ceiling):
    candidate_by_id = {item["identity"]: item for item in candidate}
    ceiling_by_id = {item["identity"]: item for item in ceiling}
    new_ids = sorted(set(candidate_by_id) - set(ceiling_by_id))
    removed_ids = sorted(set(ceiling_by_id) - set(candidate_by_id))
    return {
        "ok": not new_ids,
        "new": [candidate_by_id[item] for item in new_ids],
        "removed": [ceiling_by_id[item] for item in removed_ids],
    }


def _finding_summary(finding):
    return (f"{finding['owner']} {finding['category']}: "
            f"{finding['subject']}")


def evaluate_isolation(findings, science_nodes, reference, ratchet_ids):
    science = [item for item in findings if item["owner"] in science_nodes]
    non_science = [item for item in findings if item["owner"] not in science_nodes]
    tier_a_categories = {
        "forbidden_runtime_reference", "forbidden_rpath", "forbidden_string",
        "unresolved_dependency", "external_dependency",
        "dynamic_science_dependency", "main_reaches_science_dependency",
    }
    tier_a_failures = [
        item for item in findings
        if item["category"] == "main_reaches_science_dependency" or (
            item["owner"] in science_nodes and
            item["category"] in tier_a_categories)]
    reference_by_id = {item["identity"]: item for item in reference["findings"]}
    ceiling = [reference_by_id[item] for item in sorted(ratchet_ids)]
    delta = compare_identity_sets(non_science, ceiling)
    return {
        "tier_a": {
            "status": "PASS" if not tier_a_failures else "STOP",
            "violations": tier_a_failures,
        },
        "tier_b": {"status": "PASS" if delta["ok"] else "STOP"},
        "delta": delta,
        "absolute": {"science": science, "non_science": non_science},
    }


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


def reachable_nodes(graph, start, science_plugin_name=None, source_roots=None):
    pending = [(start, False)]
    visited = set()
    reached = set()
    findings = []
    source_roots = list(source_roots or [])
    while pending:
        node, below_science = pending.pop()
        state = (node, below_science)
        if state in visited:
            continue
        visited.add(state)
        reached.add(node)
        below_science = below_science or Path(node).name == science_plugin_name
        if SCIENCE_DEPENDENCY_PATTERN.search(Path(node).name) and not below_science:
            message = (
                f"science dependency reachable from main outside "
                f"{science_plugin_name}: {node}")
            finding = make_finding(
                start, "main_reaches_science_dependency", node, source_roots)
            finding["message"] = message
            findings.append(finding)
        for edge in graph.get(node, []):
            resolved = edge.get("resolved")
            if resolved:
                pending.append((resolved, below_science))
    return reached, findings


def audit_bundle(app, baseline, inspector=None, source_roots=None,
                 main_relative=None,
                 science_plugin_name="osgdb_science.so",
                 require_science_plugin=True,
                 reference_manifest=None, ratchet_manifest=None):
    app = Path(app).resolve()
    baseline = Path(baseline).resolve()
    inspector = inspector or CommandInspector()
    source_roots = list(source_roots or [])
    if not app.is_dir() or not baseline.is_dir():
        raise ValueError("app and baseline must both be existing directories")
    if (reference_manifest is None) != (ratchet_manifest is None):
        raise ValueError("reference and ratchet manifests must be provided together")
    if reference_manifest is not None:
        MANIFEST.validate_chain(reference_manifest, ratchet_manifest)
        if reference_manifest["bundle_fingerprint"] != MANIFEST.bundle_fingerprint(
                baseline):
            raise ValueError("reference bundle fingerprint does not match baseline")
    main_relative = main_relative or infer_main_relative(app)
    executable = (app / main_relative).resolve()
    if not executable.is_file():
        raise ValueError(f"main executable is missing: {main_relative}")

    binaries = discover_macho(app, inspector)
    binary_by_relative = {internal_relative(path, app): path for path in binaries}
    if main_relative not in binary_by_relative:
        raise ValueError("main executable is not a Mach-O file")
    findings = []
    unresolved = []

    def add_finding(owner, category, subject, message):
        finding = make_finding(owner, category, subject, source_roots)
        finding["message"] = message
        findings.append(finding)

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
    if len(science_nodes) != 1 and require_science_plugin:
        add_finding(
            science_plugin_name, "missing_science_plugin", len(science_nodes),
            f"expected exactly one {science_plugin_name}, found {len(science_nodes)}")

    for relative, item in sorted(metadata.items()):
        exempt = relative == science_relative
        for raw in item["dependencies"]:
            forbidden = is_forbidden_reference(raw, source_roots)
            if forbidden:
                add_finding(
                    relative, "forbidden_runtime_reference", raw,
                    f"{forbidden} runtime reference in {relative}: {raw}")
            if SCIENCE_DEPENDENCY_PATTERN.search(Path(raw).name) and not exempt:
                add_finding(
                    relative, "dynamic_science_dependency", raw,
                    f"dynamic science dependency in {relative}: {raw}")
        for rpath in item["rpaths"]:
            forbidden = is_forbidden_reference(rpath, source_roots)
            if forbidden:
                add_finding(
                    relative, "forbidden_rpath", rpath,
                    f"{forbidden} rpath in {relative}: {rpath}")
        for value in item["strings"]:
            forbidden = is_forbidden_reference(value, source_roots)
            if forbidden:
                add_finding(
                    relative, "forbidden_string", value,
                    f"{forbidden} string in {relative}: {value}")
            if SCIENCE_STRING_PATTERN.search(value) and not exempt:
                add_finding(
                    relative, "static_science_string", value,
                    f"static science string in {relative}: {value}")
        for value in item["symbols"]:
            if SCIENCE_SYMBOL_PATTERN.search(value) and not exempt:
                add_finding(
                    relative, "static_science_symbol", value,
                    f"static science symbol in {relative}: {value}")

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
                add_finding(relative, "unresolved_dependency", raw, message)
            elif external:
                add_finding(
                    relative, "external_dependency", f"{raw} -> {external}",
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

    _, main_science_findings = reachable_nodes(
        graph, main_relative, science_plugin_name, source_roots)
    findings.extend(main_science_findings)
    if science_relative is None:
        science_reachable = set(science_nodes)
    else:
        science_reachable, _ = reachable_nodes(
            graph, science_relative, science_plugin_name, source_roots)
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
    unresolved = sorted(set(unresolved))
    findings_by_id = {item["identity"]: item for item in findings}
    findings = [findings_by_id[item] for item in sorted(findings_by_id)]
    if reference_manifest is None:
        policy = {
            "tier_a": {"status": "NOT_EVALUATED", "violations": []},
            "tier_b": {"status": "NOT_EVALUATED"},
            "delta": {"ok": True, "new": [], "removed": []},
            "absolute": {"science": [], "non_science": findings},
        }
        manifests = {"reference": None, "ratchet": None}
        policy_violations = [item["message"] for item in findings]
    else:
        # Baseline-owned libraries remain Tier B even when the new science plugin
        # links them. Tier A owns only the plugin's bundle delta closure.
        policy = evaluate_isolation(
            findings, set(science_only), reference_manifest,
            MANIFEST.current_finding_ids(ratchet_manifest))
        release = ratchet_manifest["releases"][-1]
        manifests = {
            "reference": {
                "source_commit": reference_manifest["source_commit"],
                "bundle_fingerprint": reference_manifest["bundle_fingerprint"],
                "finding_ids": list(reference_manifest["finding_ids"]),
            },
            "ratchet": {
                "boundary_commit": ratchet_manifest["boundary_commit"],
                "reference_sha256": ratchet_manifest["reference_sha256"],
                "release_tag": release["release_tag"],
                "finding_ids": list(release["finding_ids"]),
            },
        }
        policy_violations = [
            item.get("message", _finding_summary(item))
            for item in policy["tier_a"]["violations"] + policy["delta"]["new"]]
    violations = sorted(size_result["violations"] + policy_violations)
    policy_stops = (
        policy["tier_a"]["status"] == "STOP" or
        policy["tier_b"]["status"] == "STOP")
    if violations or policy_stops:
        status = "STOP"
    else:
        status = size_result["status"]
    return {
        "schema_version": 3,
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
        "findings": findings,
        "manifests": manifests,
        "tier_a": policy["tier_a"],
        "tier_b": policy["tier_b"],
        "absolute": policy["absolute"],
        "delta": policy["delta"],
        "violations": violations,
    }


def render_text(result):
    if "error" in result:
        return (
            f"ScienceEarth macOS bundle audit: {result['status']}\n"
            f"Error: {result['error']}\n")
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
    lines.extend(["", "Isolation manifests"])
    for label in ("reference", "ratchet"):
        value = result["manifests"][label]
        lines.append(f"  {label}: {'none' if value is None else json.dumps(value, sort_keys=True)}")
    lines.extend(["", "Tier A science closure"])
    lines.append(f"  status: {result['tier_a']['status']}")
    lines.extend(
        f"  {_finding_summary(item)}" for item in result["tier_a"]["violations"])
    if not result["tier_a"]["violations"]:
        lines.append("  none")
    lines.extend(["", "Tier B non-science delta"])
    lines.append(f"  status: {result['tier_b']['status']}")
    lines.extend(["", "Historical absolute debt"])
    lines.extend(
        f"  {_finding_summary(item)}" for item in result["absolute"]["non_science"])
    if not result["absolute"]["non_science"]:
        lines.append("  none")
    lines.extend(["", "Removed debt"])
    lines.extend(
        f"  {_finding_summary(item)}" for item in result["delta"]["removed"])
    if not result["delta"]["removed"]:
        lines.append("  none")
    lines.extend(["", "New findings"])
    lines.extend(f"  {_finding_summary(item)}" for item in result["delta"]["new"])
    if not result["delta"]["new"]:
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
    parser.add_argument("--reference-manifest")
    parser.add_argument("--ratchet-manifest")
    parser.add_argument("--source-root", action="append", default=[])
    return parser.parse_args(argv)


def load_json_object(path, label):
    if not path:
        raise ValueError(f"missing required {label} manifest")
    with Path(path).open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{label} manifest must be an object")
    return value


def error_result(message, reference_path=None, ratchet_path=None):
    return {
        "schema_version": 3,
        "ok": False,
        "status": "STOP",
        "exit_code": 1,
        "error": str(message),
        "manifests": {
            "reference": reference_path,
            "ratchet": ratchet_path,
        },
    }


def main(argv=None):
    arguments = parse_arguments(argv)
    app = Path(arguments.app).resolve()
    baseline = Path(arguments.baseline).resolve()
    source_roots = arguments.source_root or [Path(__file__).resolve().parents[1]]
    json_path = Path(arguments.json)
    text_path = Path(arguments.text) if arguments.text else json_path.with_suffix(".txt")
    json_path.parent.mkdir(parents=True, exist_ok=True)
    text_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        reference = load_json_object(
            arguments.reference_manifest, "reference")
        ratchet = load_json_object(arguments.ratchet_manifest, "ratchet")
        MANIFEST.validate_chain(reference, ratchet)
        MANIFEST.validate_normalization_profile(reference, len(source_roots))
        if reference["bundle_fingerprint"] != MANIFEST.bundle_fingerprint(baseline):
            raise ValueError("reference bundle fingerprint does not match baseline")
        result = audit_bundle(
            app=app,
            baseline=baseline,
            source_roots=source_roots,
            reference_manifest=reference,
            ratchet_manifest=ratchet,
        )
    except (OSError, json.JSONDecodeError, ValueError) as error:
        result = error_result(
            f"manifest validation failed: {error}",
            arguments.reference_manifest,
            arguments.ratchet_manifest,
        )
    json_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    report = render_text(result)
    text_path.write_text(report)
    sys.stdout.write(report)
    return result["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
