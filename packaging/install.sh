#!/bin/sh
# Invoked by Install.command; copies a fixed payload, never writes system kexts.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
[ "$(id -u)" = 0 ] || { echo 'Executer avec sudo.' >&2; exit 1; }
[ "$(sw_vers -productVersion)" = 10.3.9 ] && [ "$(uname -p)" = powerpc ] || { echo 'Ce paquet exige Mac OS X 10.3.9 PowerPC.'; exit 1; }
kit=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
base='/Library/Application Support/DWA121'
bin=/usr/local/libexec/dwa121
startup=/Library/StartupItems/DWA121
for path in "$base" "$bin" "$startup"; do
    if [ -e "$path" ] || [ -L "$path" ]; then
        echo "Installation interrompue : $path existe deja. Utiliser d'abord Desinstaller.command." >&2
        exit 1
    fi
done
if kextstat | grep -q 'local.dwa121.'; then echo 'Arreter et decharger les essais DWA121 avant installation.' >&2; exit 1; fi
for tool in dwa121-connect dwa121-config dwa121-service; do
    [ -f "$kit/payload/$tool" ] && [ ! -L "$kit/payload/$tool" ] || exit 1
done
[ -f "$kit/payload/DWA121Diagnostic.kext/Contents/Info.plist" ] || exit 1
umask 077
mkdir -p /usr/local/libexec /Library/StartupItems
mkdir "$base" "$bin"
complete=0
rollback() {
    if [ "$complete" = 0 ]; then rm -rf "$base" "$bin" "$startup"; fi
}
trap rollback 0
trap 'exit 1' 1 2 15
cp -R "$kit/payload/DWA121Diagnostic.kext" "$base/"
cp "$kit/payload/DWA121Diagnostic.kext/Contents/Info.plist" "$base/Info.template.plist"
for tool in dwa121-connect dwa121-config dwa121-service; do cp "$kit/payload/$tool" "$bin/$tool"; done
cp "$kit/packaging/prepare.sh" "$bin/prepare.sh"
cp "$kit/packaging/unload.sh" "$bin/unload.sh"
cp "$kit/packaging/uninstall.sh" "$bin/uninstall.sh"
cp -R "$kit/packaging/DWA121" "$startup"
printf '%s\n' 'DWA121-PANTHER-0.15.0' > "$base/receipt"
chown -R root:wheel "$base" "$bin" "$startup"
chmod 700 "$base"
chmod -R go-w "$base" "$bin" "$startup"
chmod 755 "$bin" "$bin/"* "$startup" "$startup/DWA121"
chmod 644 "$startup/StartupParameters.plist"
# The private parent remains 0700; kext members use conventional permissions.
find "$base/DWA121Diagnostic.kext" -type d -exec chmod 755 '{}' \;
find "$base/DWA121Diagnostic.kext" -type f -exec chmod 644 '{}' \;
chmod 755 "$base/DWA121Diagnostic.kext/Contents/MacOS/DWA121Diagnostic"
complete=1
printf 'Installation terminee. Configurer le reseau avant de demarrer le service.\n'
