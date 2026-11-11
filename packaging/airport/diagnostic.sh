#!/bin/sh
# Read-only system collection; writes only a new diagnostic archive.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
umask 077
base=/usr/local/libexec/dwa121-airport/0.3.4
state=/var/db/dwa121-airport
[ "$(id -u)" = 0 ] && [ -d "$state" ] && [ ! -L "$state" ] || exit 1
[ -n "$(find "$state" -prune -user root -perm 0700 -print)" ] || exit 1
work=$(mktemp -d "$state/diagnostic.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
{ sw_vers; uname -p; date -u; } > "$work/system.txt"
kextstat | grep -i 'local.dwa121\|local.airportp02' > "$work/kexts.txt" || true
"$base/bin/dwa121-airport-diagnostic" > "$work/radio.txt" 2>&1 || true
"$base/bin/dwa121-airport-supervisor" status > "$work/runtime.txt" 2>&1 || true
for log in runtime.log runtime.previous.log boot.log boot.previous.log; do
    if [ -f "$state/$log" ] && [ ! -L "$state/$log" ]; then
        tail -400 "$state/$log" > "$work/$log.txt"
    fi
done
if [ -f /var/log/system.log ]; then
    # Filter repetitive read-only polling before truncation: otherwise a few
    # minutes of normal menu activity erase every initialization event.
    grep 'kernel: DWA121' /var/log/system.log |
        grep -v 'AirPort P02: caller=.*op=18 in=1 out=56\|AirPort P02: caller=.*op=3 in=9 out=12' |
        tail -300 > "$work/driver-recent.txt" || true
    grep -i 'DWA121' /var/log/system.log | grep -v 'kernel:' |
        tail -100 > "$work/project-userspace-recent.txt" || true
fi
if [ -f /Library/Logs/panic.log ]; then
    grep 'panic(cpu\|Darwin Kernel Version\|local.dwa121\|local.airportp02' /Library/Logs/panic.log | tail -60 > "$work/panic-project-summary.txt" || true
fi
cp "$base/COMPONENTS.txt" "$work/COMPONENTS.txt"
archive="$work.tgz"
tar -czf "$archive" -C "$work" .
chmod 600 "$archive"
echo "Diagnostic archive (root only): $archive"
echo 'Contains project radio/network state and recent project logs; no Wi-Fi profiles or keys.'
echo 'Copy this archive with administrator privileges to share it.'
