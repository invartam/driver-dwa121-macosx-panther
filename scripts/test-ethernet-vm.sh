#!/bin/sh
# VM ONLY. Exercises real IONetworkingFamily with a synthetic, non-USB provider.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
[ "${DWA_ETHERNET_FIXTURE_VM:-}" = 1 ] && [ "$(id -u)" = 0 ] || exit 1
[ "$(uname -p)" = powerpc ] && [ "$(sw_vers -productVersion)" = 10.3.9 ] || exit 1
cd "$(dirname "$0")/.."
if kextstat | grep -q local.dwa121; then echo 'DWA module already loaded; refusing fixture.' >&2; exit 1; fi
work=$(mktemp -d /private/tmp/dwa121-ethernet-vm.XXXXXX)
bsd=
cleanup() {
    code=$?
    trap - 0 1 2 15
    set +e
    for bundle in DWA121Ethernet EthernetTestProvider DWA121Diagnostic; do
        case "$bundle" in
            DWA121Ethernet) module=local.dwa121.Ethernet ;;
            EthernetTestProvider) module=local.dwa121.EthernetTestProvider ;;
            DWA121Diagnostic) module=local.dwa121.Diagnostic ;;
        esac
        if kextstat | grep -F "$module (" >/dev/null; then
            kextunload "$work/$bundle.kext"
        fi
    done
    if kextstat | grep -q local.dwa121; then
        code=1;echo "Loaded module remains; preserve $work" >&2
    else
        rm -rf "$work"
    fi
    exit "$code"
}
trap cleanup 0
trap 'exit 1' 1 2 15
cp -R /Users/builder/dwa121/native/build/DWA121Diagnostic.kext "$work/"
# Export the subclass ABI only in this temporary copy, with NO USB matching.
# Otherwise a still-attached dongle could create a second real provider.
cp tests/ethernet_diagnostic_symbols.plist "$work/DWA121Diagnostic.kext/Contents/Info.plist"
cp -R build/ethernet/DWA121Ethernet.kext build/ethernet/EthernetTestProvider.kext "$work/"
chown -R root:wheel "$work"
chmod -R go-w "$work"
kextload -c -t "$work/DWA121Diagnostic.kext"
kextload -c -t -r "$work" "$work/EthernetTestProvider.kext"
kextload -c -t "$work/DWA121Ethernet.kext"
i=0
until build/ethernet/dwa121-netcheck > build/ethernet/vm-attachment.log 2>&1; do
    i=$((i+1));[ "$i" -lt 30 ] || { cat build/ethernet/vm-attachment.log; exit 1; }
    sleep 1
done
cat build/ethernet/vm-attachment.log
bsd=$(awk '/^Interface: / { print $2 }' build/ethernet/vm-attachment.log)
[ -n "$bsd" ] || exit 1
if ! build/ethernet/dwa121-netcheck --deny-up; then
    ifconfig "$bsd"
    ioreg -p IOService -c DWA121Ethernet -w 0
    exit 1
fi
ifconfig "$bsd"
# Terminate the provider first to exercise recursive BSD/interface teardown.
kextunload "$work/EthernetTestProvider.kext"
i=0
while ifconfig "$bsd" >/dev/null 2>&1; do
    i=$((i+1));[ "$i" -lt 30 ] || exit 1;sleep 1
done
build/ethernet/dwa121-netcheck --absent
kextunload "$work/DWA121Ethernet.kext"
kextunload "$work/DWA121Diagnostic.kext"
if kextstat | grep -q local.dwa121; then exit 1; fi
echo 'PASS: synthetic provider teardown removes BSD interface and all modules'
