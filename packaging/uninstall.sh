#!/bin/sh
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
[ "$(id -u)" = 0 ] || exit 1
base='/Library/Application Support/DWA121'
bin=/usr/local/libexec/dwa121
startup=/Library/StartupItems/DWA121
[ -d "$base" ] && [ ! -L "$base" ] && [ -f "$base/receipt" ] || { echo 'Installation DWA121 geree introuvable.'; exit 1; }
[ "$(cat "$base/receipt")" = DWA121-PANTHER-0.15.0 ] || { echo 'Recu non reconnu.'; exit 1; }
"$bin/dwa121-service" stop
if kextstat | grep -q 'local.dwa121.'; then
    echo 'Le module reste charge : aucun fichier supprime. Decharger le module ou redemarrer puis reessayer.' >&2; exit 1
fi
rm -rf "$startup" "$base" "$bin"
echo 'DWA121 desinstalle ; configuration et cle derivee supprimees.'
