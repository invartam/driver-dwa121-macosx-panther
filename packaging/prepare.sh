#!/bin/sh
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
base='/Library/Application Support/DWA121'
[ "$(id -u)" = 0 ] || exit 1
if kextstat | grep -q 'local.dwa121.'; then echo 'DWA121 module already loaded.' >&2; exit 1; fi
channel=$(/usr/local/libexec/dwa121/dwa121-config --channel)
case "$channel" in 1|2|3|4|5|6|7|8|9|10|11) ;; *) exit 1 ;; esac
awk -v channel="$channel" '
 /<key>PassiveRXChannel<\/key>/ { pending=1; print; next }
 pending { if ($0 !~ /<integer>[0-9]+<\/integer>/) exit 2;
           print "<integer>" channel "</integer>"; pending=0; changed++; next }
 /<key>PersistentRX<\/key>/ { print "<key>AssociationTXEnabled</key><true/><key>WPA2ProbeEnabled</key><true/><key>CCMPDataProbeEnabled</key><true/><key>NetworkSessionEnabled</key><true/>" }
 { print }
 END { if(changed!=1 || pending) exit 2 }
' "$base/Info.template.plist" > "$base/Info.next.plist"
mv "$base/Info.next.plist" "$base/DWA121Diagnostic.kext/Contents/Info.plist"
chmod 644 "$base/DWA121Diagnostic.kext/Contents/Info.plist"
