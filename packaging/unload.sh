#!/bin/sh
# Panther kextunload can report failure while exiting zero. Check kernel state.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
[ "$(id -u)" = 0 ] || exit 1
n=0
while kextstat | grep -F 'local.dwa121.Diagnostic (' >/dev/null; do
    kextunload '/Library/Application Support/DWA121/DWA121Diagnostic.kext'
    if ! kextstat | grep -F 'local.dwa121.Diagnostic (' >/dev/null; then exit 0; fi
    n=$((n+1));[ "$n" -lt 20 ] || exit 1
    sleep 1
done
exit 0
