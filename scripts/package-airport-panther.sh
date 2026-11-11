#!/bin/sh
# Run natively in Panther after building all runtime components from source.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
[ "$(id -u)" = 0 ] && [ "$(uname -p)" = powerpc ] && [ "$(sw_vers -productVersion)" = 10.3.9 ] || exit 1
cd "$(dirname "$0")/.."
project=$(pwd)
name=DWA121-AirPort-P0.3.4-Panther-PPC
mkdir -p build/package-airport
stage=$(mktemp -d "$project/build/package-airport/stage.XXXXXX")
root="$stage/root"
base="$root/usr/local/libexec/dwa121-airport/0.3.4"
apps="$root/Applications/DWA121 AirPort Preview"
resources="$stage/resources"
mkdir -p "$base/bin" "$base/Licenses" "$apps" "$resources"
cp -Rp build/DWA121Diagnostic.kext build/airport-p02/DWA121AirPortP02.kext "$base/"
cp build/dwa121-connect "$base/bin/"
for tool in dwa121-airport-broker dwa121-airport-monitor dwa121-airport-service dwa121-airport-supervisor dwa121-airport-diagnostic airport-status-probe; do
    cp "build/airport-p02/$tool" "$base/bin/"
done
cp build/airport-p02/airport-preferences-test "$base/bin/dwa121-preferences-compare"
cp packaging/airport/manage.sh packaging/airport/diagnostic.sh "$base/"
cp -Rp packaging/airport/StartupItem "$base/"
printf 'DWA121-AIRPORT-P034\n' > "$base/OWNER"
cp "$base/OWNER" "$apps/OWNER"
cp LICENSE "$base/Licenses/Project.txt"
cp firmware/LICENSE.Realtek "$base/Licenses/Realtek.txt"
cp vendor/wpa_supplicant-2.12/COPYING "$base/Licenses/hostap.txt"
cp src/rtl8188cu/NOTICE "$base/Licenses/OpenBSD.txt"
cp docs/airport-p034-install-powerbook.md "$apps/Instructions.txt"
for entry in Activer:activate Desactiver:deactivate Restaurer:rollback Desinstaller:uninstall Etat:status Activer-au-demarrage:enable-boot Desactiver-au-demarrage:disable-boot Diagnostic:diagnostic; do
    label=${entry%%:*}; action=${entry#*:}
    launcher="$apps/$label.command"
    printf '#!/bin/sh\n' > "$launcher"
    if [ "$action" = diagnostic ]; then
        printf 'sudo /usr/local/libexec/dwa121-airport/0.3.4/diagnostic.sh\n' >> "$launcher"
    else
        printf 'sudo /usr/local/libexec/dwa121-airport/0.3.4/manage.sh %s\nresult=$?\n' "$action" >> "$launcher"
        if [ "$action" = activate ]; then printf '[ "$result" = 0 ] && killall SystemUIServer\n' >> "$launcher"; fi
    fi
    printf 'printf "Appuyer sur Entree pour fermer. "\nread -r ignored\n' >> "$launcher"
done
cp packaging/airport/preflight packaging/airport/postflight "$resources/"
{
    echo 'P0.3.4 experimental runtime; built natively for Panther PPC32.'
    sw_vers; gcc --version | head -1
    echo 'Core 0.15.3; AirPort companion 0.3.4; WPA2 hostap 2.12, Panther patches.'
    echo 'USB 1.1 PowerBook qualification pending.'
    for binary in "$base"/*.kext/Contents/MacOS/* "$base"/bin/*; do
        file "$binary"
        file "$binary" | grep -q 'Mach-O.*ppc' || exit 1
        otool -L "$binary"
    done
} > "$base/COMPONENTS.txt"
chown -R root:wheel "$root" "$resources"
find "$root" -type d -exec chmod 755 {} \;
find "$root" -type f -exec chmod 644 {} \;
chmod 755 "$base"/bin/* "$base"/*.sh "$base"/*.kext/Contents/MacOS/* "$apps"/*.command "$base/StartupItem/DWA121AirPortPreview" "$resources/preflight" "$resources/postflight"
pkg="$stage/$name.pkg"
/Developer/Applications/Utilities/PackageMaker.app/Contents/MacOS/PackageMaker -build -p "$pkg" -f "$root" -r "$resources" -i packaging/airport/PackageInfo.plist -d packaging/airport/Description.plist
for part in Info.plist Archive.bom Archive.pax.gz; do test -f "$pkg/Contents/$part"; done
mkdir "$stage/disk"
cp -Rp "$pkg" "$stage/disk/"
cp "$apps/Instructions.txt" "$stage/disk/Instructions.txt"
hdiutil create -srcfolder "$stage/disk" -volname DWA121-AirPort-P034 -format UDZO "$stage/$name.dmg"
tar -czf "$stage/$name.tgz" -C "$stage" "$name.pkg"
chmod 755 "$stage"
chmod 644 "$stage/$name.dmg" "$stage/$name.tgz"
echo "Candidate package and archives: $stage"
echo 'Not qualified for publication until Installer, network and rollback tests pass.'
