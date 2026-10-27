#!/bin/sh
# Build the upstream RSN core and our userland adapter, on host or Panther.
set -eu
cd "$(dirname "$0")/.."
out=${WPA_BUILD_DIR:-artifacts/wpa-core-host}
mkdir -p "$out"
up=vendor/wpa_supplicant-2.12/src
compiler=${CC:-cc}
flags="-std=gnu99 -O2 -Wall -include src/supplicant/platform.h -DCONFIG_NO_RANDOM_POOL -DCONFIG_NO_WPA_MSG -DCONFIG_NO_STDOUT_DEBUG -DCONFIG_CRYPTO_INTERNAL -I$up -I$up/utils"
objects=
for source in \
    rsn_supp/wpa.c rsn_supp/wpa_ie.c rsn_supp/pmksa_cache.c rsn_supp/preauth.c \
    common/wpa_common.c common/ieee802_11_common.c \
    utils/common.c utils/wpa_debug.c utils/wpabuf.c utils/bitfield.c utils/eloop.c utils/os_unix.c \
    crypto/aes-internal.c crypto/aes-internal-enc.c crypto/aes-internal-dec.c \
    crypto/aes-unwrap.c crypto/aes-wrap.c crypto/aes-omac1.c crypto/aes-ccm.c \
    crypto/sha1.c crypto/sha1-internal.c crypto/sha1-prf.c crypto/sha1-pbkdf2.c \
    crypto/sha256.c crypto/sha256-internal.c crypto/sha256-prf.c \
    crypto/md5.c crypto/md5-internal.c crypto/rc4.c
do
    object="$out/$(echo "$source" | tr / _).o"
    $compiler $flags ${WPA_EXTRA_CFLAGS:-} -c "$up/$source" -o "$object"
    objects="$objects $object"
done
for source in src/net80211/beacon.c src/net80211/station.c src/net80211/eapol.c src/net80211/dhcp_probe.c src/supplicant/wpa2.c src/supplicant/session.c src/supplicant/data.c
do
    object="$out/$(basename "$source" .c).o"
    $compiler $flags ${WPA_EXTRA_CFLAGS:-} -Werror -c "$source" -o "$object"
    objects="$objects $object"
done
ar rcs "$out/libdwa-wpa2.a" $objects
echo "Built $out/libdwa-wpa2.a"
