#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out=${WPA_BUILD_DIR:-artifacts/wpa-core-host}
compiler=${CC:-cc}
sh scripts/build-wpa-core.sh
up=vendor/wpa_supplicant-2.12/src
flags="-std=gnu99 -Wall -Werror -include src/supplicant/platform.h -isystem $up -isystem $up/utils"
# The independent AP test oracle uses the system OpenSSL on Panther.
# On the host, select Homebrew OpenSSL without changing global search paths.
crypto_flags=
if [ "$(uname -m)" != Power\ Macintosh ] && [ -d /opt/homebrew/opt/openssl@3/include ]; then
    crypto_flags='-Wno-deprecated-declarations -I/opt/homebrew/opt/openssl@3/include -L/opt/homebrew/opt/openssl@3/lib'
fi
$compiler $flags ${WPA_EXTRA_CFLAGS:-} src/net80211/beacon.c src/net80211/station.c tests/station_test.c -o "$out/station-test"
"$out/station-test"
$compiler $flags ${WPA_EXTRA_CFLAGS:-} $crypto_flags tests/wpa2_test.c "$out/libdwa-wpa2.a" -lcrypto -o "$out/wpa2-test"
"$out/wpa2-test"
$compiler $flags ${WPA_EXTRA_CFLAGS:-} $crypto_flags tests/data_test.c "$out/libdwa-wpa2.a" -lcrypto -o "$out/data-test"
"$out/data-test"
