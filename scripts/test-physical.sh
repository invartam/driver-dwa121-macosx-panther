#!/bin/sh
# Run from the extracted Panther archive. No persistent installation.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
usage() {
    echo 'Usage: sudo sh scripts/test-physical.sh rx CHANNEL [--usb11]' >&2
    echo '       sudo sh scripts/test-physical.sh associate CHANNEL SSID [--usb11]' >&2
    echo '       sudo sh scripts/test-physical.sh wpa CHANNEL SSID [--usb11]' >&2
    echo '       sudo sh scripts/test-physical.sh data CHANNEL SSID [--usb11]' >&2
    echo '       sudo sh scripts/test-physical.sh interface CHANNEL [--usb11]  (requires Ethernet companion)' >&2
    exit 1
}
[ "$#" -ge 2 ] || usage
mode=$1
channel=$2
shift 2
case "$channel" in 1|2|3|4|5|6|7|8|9|10|11) ;; *) usage ;; esac
ssid=
case "$mode" in
    rx|interface) ;;
    associate|wpa|data) [ "$#" -ge 1 ] || usage; ssid=$1; shift; [ -n "$ssid" ] || usage ;;
    *) usage ;;
esac
usb11=
if [ "$#" -gt 0 ]; then [ "$1" = --usb11 ] || usage; usb11=--usb11; shift; fi
[ "$#" -eq 0 ] || usage
[ "$(id -u)" = 0 ] || { echo 'Lancer avec sudo.' >&2; exit 1; }
[ "$(uname -s)" = Darwin ] && [ "$(uname -p)" = powerpc ] && [ "$(sw_vers -productVersion)" = 10.3.9 ] || {
    echo 'Cet essai exige Mac OS X 10.3.9 PowerPC.' >&2; exit 1;
}
kit=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
for tool in dwa121-info dwa121-rx dwa121-associate dwa121-check dwa121-wpa2; do
    [ -x "$kit/$tool" ] || { echo "Outil manquant : $kit/$tool" >&2; exit 1; }
done
[ -d "$kit/DWA121Diagnostic.kext" ] || exit 1
if [ "$mode" = interface ]; then
    [ -d "$kit/DWA121Ethernet.kext" ] && [ -x "$kit/dwa121-netcheck" ] || {
        echo 'Le mode interface exige le module compagnon Ethernet et dwa121-netcheck.' >&2; exit 1;
    }
fi
if kextstat | grep -q 'local.dwa121.Diagnostic'; then
    echo 'Un diagnostic est deja charge. Arreter et decharger cet essai avant de continuer.' >&2; exit 1
fi
if kextstat | grep -q 'local.dwa121.Ethernet'; then
    echo 'Un module Ethernet DWA est deja charge. Decharger cet essai avant de continuer.' >&2; exit 1
fi
umask 077
results="$PWD/dwa121-results-$(date +%Y%m%d-%H%M%S)-$$"
mkdir "$results"
work=$(mktemp -d /private/tmp/dwa121-test.XXXXXX)
kext="$work/DWA121Diagnostic.kext"
attempted=0
companion_attempted=0
bsd=
cleanup() {
    code=$?
    trap - 0 1 2 15
    set +e
    if [ "$companion_attempted" = 1 ]; then
        kextunload "$work/DWA121Ethernet.kext" > "$results/ethernet-unload.log" 2>&1 || code=1
        j=0
        while [ -n "$bsd" ] && ifconfig "$bsd" >/dev/null 2>&1; do
            j=$((j+1));[ "$j" -lt 30 ] || { code=1; break; };sleep 1
        done
        "$kit/dwa121-netcheck" --absent > "$results/ethernet-absent.log" 2>&1 || code=1
    fi
    if [ "$attempted" = 1 ]; then
        if kextstat | grep -q 'local.dwa121.Diagnostic'; then
            "$kit/dwa121-rx" stop > "$results/stop.log" 2>&1
            "$kit/dwa121-info" > "$results/stopped.plist" 2> "$results/snapshot-error.log"
            "$kit/dwa121-check" "$results/stopped.plist" stopped $usb11 > "$results/cleanup-check.log" 2>&1 || code=1
            if [ "$code" = 0 ]; then
                check=rx
                [ "$mode" = associate ] && check=association
                [ "$mode" = wpa ] && check=wpa
                [ "$mode" = data ] && check=data
                "$kit/dwa121-check" "$results/stopped.plist" "$check" $usb11 > "$results/result-check.log" 2>&1 || code=1
            fi
            kextunload "$kext" > "$results/unload.log" 2>&1 || code=1
        else
            code=1
        fi
    fi
    kextstat > "$results/kextstat-after.txt"
    if grep -q 'local.dwa121.' "$results/kextstat-after.txt"; then
        code=1
        echo "Le module reste charge. Copie conservee : $kext" >&2
    else
        rm -rf "$work"
    fi
    dmesg | grep 'DWA121\(Diagnostic\|Ethernet\):' | tail -100 > "$results/kernel-driver.log"
    echo "exit=$code" > "$results/result.txt"
    echo "mode=$mode channel=$channel usb11=$usb11" >> "$results/result.txt"
    echo "Journaux : $results"
    if [ "$code" = 0 ]; then echo 'PASS : essai termine et module decharge.'; else echo 'FAIL : consulter les journaux avant de recommencer.' >&2; fi
    # Return log ownership to the sudo caller, but never trust a textual owner.
    case "${SUDO_UID:-}" in ''|*[!0-9]*) ;; *) chown -R "$SUDO_UID" "$results" ;; esac
    exit "$code"
}
trap cleanup 0
trap 'exit 1' 1 2 15
sw_vers > "$results/system.txt"
uname -a >> "$results/system.txt"
system_profiler SPUSBDataType > "$results/usb.txt" 2>&1
echo "$kext" > "$results/temporary-kext.txt"
cp -R "$kit/DWA121Diagnostic.kext" "$kext"
# The shipped XML has one PassiveRXChannel. Fail if its structure changes.
awk -v channel="$channel" -v mode="$mode" '
 /<key>PassiveRXChannel<\/key>/ { pending=1; print; next }
 pending { if ($0 !~ /<integer>[0-9]+<\/integer>/) exit 2;
           print "<integer>" channel "</integer>"; pending=0; changed++; next }
 /<key>PersistentRX<\/key>/ { if(mode=="associate" || mode=="wpa" || mode=="data") print "<key>AssociationTXEnabled</key><true/>";
                           if(mode=="wpa" || mode=="data") print "<key>WPA2ProbeEnabled</key><true/>";
                           if(mode=="data") print "<key>CCMPDataProbeEnabled</key><true/>" }
 { print }
 END { if(changed!=1 || pending) exit 2 }
' "$kit/DWA121Diagnostic.kext/Contents/Info.plist" > "$kext/Contents/Info.plist"
cp "$kext/Contents/Info.plist" "$results/loaded-info.plist"
chown -R root:wheel "$kext"
chmod -R go-w "$kext"
attempted=1
kextload -c -t "$kext" > "$results/load.log" 2>&1
i=0
while [ "$i" -lt 60 ]; do
    if "$kit/dwa121-info" > "$results/running.plist" 2> "$results/startup.log"; then
        break
    fi
    sleep 1
    i=$((i+1))
done
if [ "$i" -eq 60 ]; then
    # Panther's ioreg has no -r option. Use -c without -l to avoid dumping
    # the enormous root IOCatalogue while retaining interface/client evidence.
    ioreg -p IOService -c IOUSBInterfaceUserClient -w 0 > "$results/usb-interface-clients.txt" 2>&1 || :
    dmesg | tail -150 > "$results/startup-kernel.log"
    echo "Le service USB n'a pas demarre. Voir startup-kernel.log et load.log." >&2
    echo "Si le journal indique 'someone already has it open', fermer Classic et les outils USB avant de recommencer." >&2
    exit 1
fi
"$kit/dwa121-check" "$results/running.plist" running $usb11 > "$results/running-check.log" 2>&1
if [ "$mode" = interface ]; then
    cp -R "$kit/DWA121Ethernet.kext" "$work/"
    chown -R root:wheel "$work/DWA121Ethernet.kext"
    chmod -R go-w "$work/DWA121Ethernet.kext"
    companion_attempted=1
    kextload -c -t "$work/DWA121Ethernet.kext" > "$results/ethernet-load.log" 2>&1
    i=0
    until "$kit/dwa121-netcheck" > "$results/ethernet-attachment.log" 2>&1; do
        i=$((i+1));[ "$i" -lt 30 ] || exit 1;sleep 1
    done
    bsd=$(awk '/^Interface: / { print $2 }' "$results/ethernet-attachment.log")
    [ -n "$bsd" ] || exit 1
    "$kit/dwa121-netcheck" --deny-up > "$results/ethernet-guard.log" 2>&1
    cat "$results/ethernet-guard.log"
    ifconfig "$bsd" > "$results/ethernet-ifconfig.txt"
elif [ "$mode" = rx ]; then
    sleep 10
elif [ "$mode" = associate ]; then
    "$kit/dwa121-associate" "$ssid" --require-m1 > "$results/association.log" 2>&1
    cat "$results/association.log"
else
    extra=
    [ "$mode" = data ] && extra=--dhcp-discover
    "$kit/dwa121-wpa2" "$ssid" $extra > "$results/wpa.log" 2>&1
    cat "$results/wpa.log"
fi
# The EXIT trap stops, checks and unloads even when a preceding command fails.
exit 0
