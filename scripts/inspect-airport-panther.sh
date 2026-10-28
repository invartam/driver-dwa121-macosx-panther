#!/bin/sh
# Read-only inventory; run on Panther. Never collect network.conf or keys.
set -eu
sw_vers
uname -a
for name in AppleAirPort AppleAirPort2 AppleAirPortFW IO80211Family; do
    path="/System/Library/Extensions/$name.kext/Contents/Info.plist"
    echo "--- $path"
    if test -f "$path"; then cat "$path"; else echo absent; fi
done
framework=/System/Library/PrivateFrameworks/Apple80211.framework/Apple80211
if test -f "$framework"; then
    md5 "$framework"
    nm -g "$framework" | grep Wireless || true
fi
menu='/System/Library/CoreServices/Menu Extras/AirPort.menu/Contents/MacOS/AirPort'
if test -f "$menu"; then md5 "$menu"; otool -L "$menu"; fi
# Panther ioreg has no -r. -c selects properties but retains the tree.
ioreg -c AirPortDWA121 -w 120
kextstat | grep -i 'airport\|dwa121' || true
