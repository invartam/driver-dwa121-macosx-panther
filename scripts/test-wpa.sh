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
# Exercise the real Mach implementation at synthetic long-uptime boundaries.
if [ "$(uname -s)" = Darwin ]; then
    $compiler $flags ${WPA_EXTRA_CFLAGS:-} -DCONFIG_NO_STDOUT_DEBUG -DCONFIG_NO_WPA_MSG \
        -Dmach_absolute_time=dwa_test_ticks -Dmach_timebase_info=dwa_test_timebase \
        -c "$up/utils/os_unix.c" -o "$out/mach-clock-test-os.o"
    $compiler $flags ${WPA_EXTRA_CFLAGS:-} tests/mach_clock_test.c "$out/mach-clock-test-os.o" -o "$out/mach-clock-test"
    "$out/mach-clock-test"
fi
