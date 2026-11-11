#!/bin/sh
# Installed root-owned, never sourced from a user-writable directory.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
umask 077
base=/usr/local/libexec/dwa121-airport/0.3.4
state=/var/db/dwa121-airport
stable=/usr/local/libexec/dwa121/dwa121-service
startup=/Library/StartupItems/DWA121AirPortPreview
fail() { echo "$*" >&2; exit 1; }
trusted_dir() {
    [ -d "$1" ] && [ ! -L "$1" ] && [ -z "$(find "$1" -prune ! -user root -print)" ] || fail "Unsafe directory: $1"
    [ -z "$(find "$1" -prune -perm +022 -print)" ] || fail "Writable directory: $1"
}
[ "$(id -u)" = 0 ] || fail 'Use the Finder command, or sudo.'
[ "$(uname -p)" = powerpc ] && [ "$(sw_vers -productVersion)" = 10.3.9 ] || fail 'Requires Panther 10.3.9 PowerPC.'
for path in /usr /usr/local /usr/local/libexec /usr/local/libexec/dwa121-airport "$base" /var/db; do trusted_dir "$path"; done
trusted_dir "$state"
[ "$(cat "$base/OWNER")" = DWA121-AIRPORT-P034 ] || fail 'Invalid installation marker.'
[ -z "$(find "$base" \( -type l -o ! -user root -o -perm +022 \) -print)" ] || fail 'Installation permissions are unsafe.'
cmd=${1:-status}
case "$cmd" in activate|boot-activate|deactivate|rollback|uninstall|status|enable-boot|disable-boot) ;; *) fail 'Unknown action.' ;; esac
loaded() { kextstat | grep -q "$1"; }
disable_boot() {
    if [ -e "$startup" ]; then
        [ ! -L "$startup" ] && [ "$(cat "$startup/OWNER")" = DWA121-AIRPORT-P034 ] || fail 'Unowned startup item; unchanged.'
        rm -rf "$startup"
    fi
    if [ -d "$state/stable-startup" ]; then
        [ ! -e /Library/StartupItems/DWA121 ] || fail 'Stable StartupItem path occupied; backup retained.'
        mv "$state/stable-startup" /Library/StartupItems/DWA121
    fi
}
deactivate() {
    disable_boot
    "$base/bin/dwa121-airport-supervisor" stop || fail 'Runtime still active. Restart Panther; do not force unload.'
    if [ -f "$state/active" ]; then
        "$base/bin/dwa121-airport-service" remove-if-present || fail 'Service cleanup failed; backup retained.'
        if loaded local.airportp02.DWA121; then kextunload "$base/DWA121AirPortP02.kext"; fi
        if loaded local.airportp02.DWA121; then fail 'Companion still loaded. Restart Panther before restoring stable.'; fi
        if loaded 'local.dwa121.Diagnostic (0.15.3)'; then kextunload "$base/DWA121Diagnostic.kext"; fi
        if loaded 'local.dwa121.Diagnostic (0.15.3)'; then fail 'Driver still loaded. Restart Panther; no forced unload.'; fi
        if [ -f "$state/preferences.before.plist" ]; then
            "$base/bin/dwa121-preferences-compare" "$state/preferences.before.plist" /Library/Preferences/SystemConfiguration/preferences.plist ||
                echo 'Other preference values changed: preserved. Original protected backup retained.'
        fi
        rm -f "$state/active"
    fi
    if [ -f "$state/stable-was-running" ]; then
        "$stable" start || fail 'Stable restart failed; installation and profile are preserved.'
        rm -f "$state/stable-was-running"
    fi
    echo 'Prototype disabled. Stable files and personal Wi-Fi profiles preserved.'
}
case "$cmd" in
status)
    kextstat | grep -i dwa121 || true
    "$base/bin/dwa121-airport-supervisor" status || true
    "$base/bin/airport-status-probe" || true
    exit 0 ;;
esac
mkdir "$state/action.lock" 2>/dev/null || fail 'Another action is running. If interrupted, restart and inspect action.lock.'
trap 'rmdir "$state/action.lock"' EXIT
case "$cmd" in
activate|boot-activate)
    "$base/bin/dwa121-airport-diagnostic" --usb-present || fail 'DWA-121 unavailable or conflicting Apple AirPort present; unchanged.'
    if [ "$cmd" = activate ]; then
        [ ! -f "$state/active" ] || fail 'Already activated or incomplete previous activation: run Deactivate first.'
    else
        [ -f "$startup/OWNER" ] && [ "$(cat "$startup/OWNER")" = DWA121-AIRPORT-P034 ] || fail 'Boot activation is not enabled.'
        "$base/bin/dwa121-airport-supervisor" status && fail 'Runtime already running.'
    fi
    loaded local.airportp02.DWA121 && fail 'An AirPort prototype is already loaded.'
    if loaded local.dwa121.Diagnostic; then
        loaded 'local.dwa121.Diagnostic (0.15.0)' || fail 'Unknown driver version loaded; unchanged.'
        "$stable" status || fail 'Driver has another owner; stop it explicitly first.'
        : > "$state/stable-was-running"
        "$stable" stop || fail 'Stable service did not stop.'
    fi
    loaded local.dwa121.Diagnostic && fail 'Previous driver remains loaded; restart required.'
    if [ "$cmd" = activate ]; then
        [ ! -e "$state/preferences.before.plist" ] || mv "$state/preferences.before.plist" "$state/preferences.previous.plist"
        cp -p /Library/Preferences/SystemConfiguration/preferences.plist "$state/preferences.before.plist"
        chmod 600 "$state/preferences.before.plist"
    else
        "$base/bin/dwa121-airport-service" remove-if-present
    fi
    : > "$state/active" # incomplete activation is explicitly recoverable
    kextload "$base/DWA121Diagnostic.kext"
    sleep 15
    kextload "$base/DWA121AirPortP02.kext"
    "$base/bin/airport-status-probe" || fail 'No working radio. Run Deactivate to restore stable.'
    "$base/bin/dwa121-airport-service" add || fail 'Service creation failed. Run Deactivate.'
    "$base/bin/dwa121-airport-supervisor" start || fail 'Runtime failed. Run Deactivate.'
    echo 'Prototype active. Refresh the menu, then select a network and enter <your password>.' ;;
deactivate) deactivate ;;
rollback)
    deactivate
    if [ -f "$state/last-install-backup" ]; then
        backup=$(cat "$state/last-install-backup")
        case "$backup" in "$state"/install-backup.*) ;; *) fail 'Invalid backup record.' ;; esac
        trusted_dir "$backup"
        if [ -d "$backup/runtime" ]; then
            [ "$(cat "$backup/runtime/OWNER")" = DWA121-AIRPORT-P034 ] || fail 'Invalid runtime backup.'
            saved=$(mktemp -d "$state/disabled-install.XXXXXX")
            mv "$base" "$saved/runtime"
            cp -Rp "$backup/runtime" "$base"
            echo 'Previous prototype files restored, inactive. Disabled version retained in protected state.'
        fi
        if [ -d "$backup/Finder" ]; then
            apps='/Applications/DWA121 AirPort Preview'
            trusted_dir "$apps"
            [ "$(cat "$apps/OWNER")" = DWA121-AIRPORT-P034 ] || fail 'Finder directory no longer owned.'
            rm -rf "$apps"; cp -Rp "$backup/Finder" "$apps"
        fi
    fi ;;
uninstall)
    deactivate
    apps='/Applications/DWA121 AirPort Preview'
    if [ -e "$apps" ]; then
        trusted_dir "$apps"
        [ "$(cat "$apps/OWNER")" = DWA121-AIRPORT-P034 ] || fail 'Finder directory no longer owned.'
        rm -rf "$apps"
    fi
    rm -rf "$base"
    echo 'Prototype removed. Protected backups and stable Wi-Fi profiles retained.' ;;
disable-boot) disable_boot ;;
enable-boot)
    [ -f "$state/active" ] || fail 'Activate and validate the prototype first.'
    "$base/bin/dwa121-airport-supervisor" status || fail 'Runtime not running.'
    [ ! -e "$startup" ] || fail 'StartupItem already exists.'
    if [ -e /Library/StartupItems/DWA121 ]; then
        [ ! -L /Library/StartupItems/DWA121 ] && [ ! -e "$state/stable-startup" ] || fail 'Unsafe stable StartupItem backup.'
        mv /Library/StartupItems/DWA121 "$state/stable-startup"
    fi
    cp -Rp "$base/StartupItem" "$startup"
    echo 'Boot activation enabled. WPA2 still requires a native password after login.' ;;
esac
