#!/bin/sh
# Finite network session; temporary root-owned extension, no installation.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
[ "$(id -u)" = 0 ] || exit 1
[ "$#" = 3 ] || { echo 'Usage: sudo sh scripts/test-network-physical.sh CHANNEL SSID SECONDS'; exit 1; }
channel=$1
ssid=$2
seconds=$3
case "$channel" in 1|2|3|4|5|6|7|8|9|10|11) ;; *) exit 1 ;; esac
case "$seconds" in ''|*[!0-9]*) exit 1 ;; esac
kit=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
if kextstat | grep -q local.dwa121.; then echo 'Un autre module DWA121 est charge.'; exit 1; fi
umask 077
work=$(mktemp -d /private/tmp/dwa121-network.XXXXXX)
results="$kit/network-results-$(date +%Y%m%d-%H%M%S)"
mkdir "$results"
kext="$work/DWA121Diagnostic.kext"
loaded=0
cleanup() {
    code=$?
    trap - 0 1 2 15
    set +e
    if [ "$loaded" = 1 ]; then
        "$kit/dwa121-info" > "$results/stopped.plist"
        kextunload "$kext" > "$results/unload.txt" 2>&1 || code=1
    fi
    if ! kextstat | grep -q local.dwa121.; then rm -rf "$work"; fi
    ifconfig -a > "$results/interfaces-after.txt"
    echo "exit=$code" > "$results/result.txt"
    case "${SUDO_UID:-}" in ''|*[!0-9]*) ;; *) chown -R "$SUDO_UID" "$results" ;; esac
    echo "Resultats : $results"
    exit "$code"
}
trap cleanup 0
trap 'exit 1' 1 2 15
cp -R "$kit/DWA121Diagnostic.kext" "$kext"
awk -v channel="$channel" '
 /<key>PassiveRXChannel<\/key>/ { pending=1; print; next }
 pending { if ($0 !~ /<integer>[0-9]+<\/integer>/) exit 2;
           print "<integer>" channel "</integer>"; pending=0; changed++; next }
 /<key>PersistentRX<\/key>/ { print "<key>AssociationTXEnabled</key><true/><key>WPA2ProbeEnabled</key><true/><key>CCMPDataProbeEnabled</key><true/><key>NetworkSessionEnabled</key><true/>" }
 { print }
 END { if(changed!=1 || pending) exit 2 }
' "$kit/DWA121Diagnostic.kext/Contents/Info.plist" > "$kext/Contents/Info.plist"
chown -R root:wheel "$work"
chmod -R go-w "$work"
ifconfig -a > "$results/interfaces-before.txt"
kextload "$kext"
loaded=1
sleep 3
"$kit/dwa121-connect" "$ssid" --seconds "$seconds"
