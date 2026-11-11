#!/bin/sh
# Build-time, public opt-ins. No saved Wi-Fi profile is read or embedded.
set -eu
[ "$#" = 2 ] || exit 1
awk '
 /<key>PersistentRX<\/key>/ {
   print "<key>AssociationTXEnabled</key><true/>"
   print "<key>WPA2ProbeEnabled</key><true/>"
   print "<key>CCMPDataProbeEnabled</key><true/>"
   print "<key>NetworkSessionEnabled</key><true/>"
   count++
 }
 { print }
 END { if(count!=1)exit 1 }
' "$1" > "$2"
