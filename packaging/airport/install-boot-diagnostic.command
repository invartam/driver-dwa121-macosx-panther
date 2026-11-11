#!/bin/sh
# Optional diagnostic-only patch for preview1; does not enable startup.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
here=$(CDPATH= cd "$(dirname "$0")" && pwd)
if [ "$(id -u)" != 0 ]; then exec sudo /bin/sh "$here/install-boot-diagnostic.command"; fi
umask 077
base=/usr/local/libexec/dwa121-airport/0.3.4
state=/var/db/dwa121-airport
[ "$(uname -p)" = powerpc ] && [ "$(sw_vers -productVersion)" = 10.3.9 ] || exit 1
for dir in /usr /usr/local /usr/local/libexec /usr/local/libexec/dwa121-airport "$base" /var/db "$state"; do
    [ -d "$dir" ] && [ ! -L "$dir" ] || exit 1
    [ -z "$(find "$dir" -prune \( ! -user root -o -perm +022 \) -print)" ] || exit 1
done
[ -n "$(find "$state" -prune -perm 0700 -print)" ] || exit 1
[ "$(cat "$base/OWNER")" = DWA121-AIRPORT-P034 ] || exit 1
[ ! -e "$state/action.lock" ] || { echo 'Another action is active or interrupted.' >&2; exit 1; }
for file in manage.sh diagnostic.sh; do
    [ -f "$here/$file" ] && [ ! -L "$here/$file" ] || exit 1
    [ -f "$base/$file" ] && [ ! -L "$base/$file" ] || exit 1
    /bin/sh -n "$here/$file"
done
backup=$(mktemp -d "$state/boot-diagnostic-backup.XXXXXX")
for file in manage.sh diagnostic.sh; do
    cp -p "$base/$file" "$backup/$file"
    install -o root -g wheel -m 755 "$here/$file" "$backup/$file.new"
done
for file in manage.sh diagnostic.sh; do mv "$backup/$file.new" "$base/$file"; done
echo "Diagnostic scripts installed; originals preserved in $backup"
echo 'No driver changed; startup has not been enabled.'
echo 'After the next boot attempt, run Diagnostic.command before deactivating.'
