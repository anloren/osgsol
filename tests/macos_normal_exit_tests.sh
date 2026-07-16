#!/bin/bash
set -euo pipefail

app="$1"
binary="$app/Contents/MacOS/osgSol_Earth"
before="$(mktemp -t osgsol-exit-before.XXXXXX)"
after="$(mktemp -t osgsol-exit-after.XXXXXX)"
find "$HOME/Library/Logs/DiagnosticReports" -name 'osgSol_Earth*.ips' -print | sort > "$before"
set +e
/usr/bin/perl -e '$seconds=shift; alarm $seconds; exec @ARGV' 30 \
    /usr/bin/env EARTH_OFFSCREEN=1 EARTH_PREFETCH=0 EARTH_IME=0 \
    EARTH_AUTOQUIT_FRAMES=5 "$binary"
status=$?
set -e
find "$HOME/Library/Logs/DiagnosticReports" -name 'osgSol_Earth*.ips' -print | sort > "$after"
test "$status" -ne 142
test "$status" -eq 0
test -z "$(comm -13 "$before" "$after")"
