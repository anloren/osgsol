#!/usr/bin/env python3
"""Fail when public-source privacy or credential material is tracked."""

from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]

CONTENT_RULES = {
    "personal macOS home path": re.compile(
        rb"/Users/(?!USER(?=[^A-Za-z0-9._-]|$)|Shared(?=[^A-Za-z0-9._-]|$))[A-Za-z0-9._-]+"
    ),
    "local-domain email": re.compile(
        rb"(?i)\b[A-Z0-9._%+-]{1,64}@[A-Z0-9.-]{1,190}\.(?:local|lan)\b"
    ),
    "private key block": re.compile(
        rb"-----BEGIN (?:RSA |EC |DSA |OPENSSH |PGP )?PRIVATE KEY-----"
        rb"[\r\n]+[A-Za-z0-9+/=\r\n]{80,}"
        rb"-----END (?:RSA |EC |DSA |OPENSSH |PGP )?PRIVATE KEY-----"
    ),
    "GitHub token": re.compile(
        rb"\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{30,})\b"
    ),
    "Google API key": re.compile(rb"\bAIza[0-9A-Za-z_-]{30,}\b"),
    "AWS access key": re.compile(rb"\b(?:AKIA|ASIA)[0-9A-Z]{16}\b"),
    "OpenAI API key": re.compile(
        rb"\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{20,}\b"
    ),
    "Anthropic API key": re.compile(rb"\bsk-ant-[A-Za-z0-9_-]{20,}\b"),
    "Slack token": re.compile(rb"\bxox[baprs]-[A-Za-z0-9-]{20,}\b"),
    "Stripe live key": re.compile(rb"\bsk_live_[0-9A-Za-z]{16,}\b"),
    "credentialed database URL": re.compile(
        rb"\b(?:postgres(?:ql)?|mysql|mongodb(?:\+srv)?|redis)://"
        rb"[^\s/:@]{1,128}:[^\s/@]{4,256}@"
    ),
}

SENSITIVE_NAMES = re.compile(
    r"(?i)(^|/)(?:\.env(?:\..*)?|id_(?:rsa|ed25519|ecdsa|dsa)(?:\..*)?|"
    r"[^/]*credentials?[^/]*|[^/]*secrets?[^/]*|[^/]*\.(?:pem|p12|pfx|key)|"
    r"[^/]*\.mobileprovision)$"
)


def git_output(*args: str) -> bytes:
    return subprocess.run(
        ["git", *args], cwd=ROOT, check=True, stdout=subprocess.PIPE
    ).stdout


def tracked_paths() -> list[str]:
    raw = git_output("ls-files", "-z")
    return [part.decode("utf-8", "surrogateescape") for part in raw.split(b"\0") if part]


def line_number(data: bytes, offset: int) -> int:
    return data.count(b"\n", 0, offset) + 1


def main() -> int:
    failures: list[str] = []

    for relative in tracked_paths():
        if SENSITIVE_NAMES.search(relative) and not relative.endswith(".env.example"):
            failures.append(f"sensitive filename: {relative}")

        path = ROOT / relative
        try:
            data = path.read_bytes()
        except OSError as exc:
            failures.append(f"unreadable tracked file: {relative}: {exc}")
            continue
        if b"\0" in data[:8192]:
            continue
        for rule, pattern in CONTENT_RULES.items():
            for match in pattern.finditer(data):
                failures.append(
                    f"{rule}: {relative}:{line_number(data, match.start())}"
                )

    identities = git_output("log", "--all", "--format=%ae%n%ce").splitlines()
    taggers = git_output(
        "for-each-ref", "refs/tags", "--format=%(taggeremail)"
    ).splitlines()
    for identity in identities + taggers:
        normalized = identity.strip().strip(b"<>").lower()
        if normalized.endswith((b".local", b".lan")):
            failures.append("local-domain identity in Git metadata")
            break

    if failures:
        print("Public-source privacy audit failed:", file=sys.stderr)
        for failure in sorted(set(failures)):
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("Public-source privacy audit passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
