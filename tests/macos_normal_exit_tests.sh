#!/bin/bash
set -euo pipefail

app="$1"
app_parent="$(cd "$(dirname "$app")" && pwd -P)"
app_path="$app_parent/$(basename "$app")"
desktop_path="$(cd "$HOME/Desktop" && pwd -P)"
case "$app_path" in
    "$desktop_path"/*)
        echo "Refusing to execute a Desktop app during automated exit testing: $app_path" >&2
        exit 64
        ;;
esac
binary="$app/Contents/MacOS/osgSol_Earth"
exit_test_frames="${OSGSOL_EXIT_TEST_FRAMES:-5}"
before="$(mktemp -t osgsol-exit-before.XXXXXX)"
after="$(mktemp -t osgsol-exit-after.XXXXXX)"
find "$HOME/Library/Logs/DiagnosticReports" -name 'osgSol_Earth*.ips' -print | sort > "$before"
set +e
/usr/bin/perl -e '$seconds=shift; alarm $seconds; exec @ARGV' 30 \
    /usr/bin/env -u EARTH_AUTOCAP EARTH_OFFSCREEN=1 EARTH_PREFETCH=0 EARTH_IME=0 \
    EARTH_AUTOQUIT_FRAMES="$exit_test_frames" "$binary"
status=$?
set -e
find "$HOME/Library/Logs/DiagnosticReports" -name 'osgSol_Earth*.ips' -print | sort > "$after"
test "$status" -ne 142
test "$status" -eq 0
test -z "$(comm -13 "$before" "$after")"
