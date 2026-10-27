#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p artifacts
COPYFILE_DISABLE=1 tar --format ustar -czf artifacts/wpa-source-transfer.tgz \
    src/net80211 src/supplicant src/control/frame_queue.h src/rtl8188cu/identity.h tests/station_test.c tests/wpa2_test.c tests/data_test.c \
    scripts/build-wpa-core.sh scripts/test-wpa.sh vendor/wpa_supplicant-2.12
scripts/vm-ssh.sh 'set -e; mkdir -p /Users/builder/dwa121/wpa-port; tar -xzf - -C /Users/builder/dwa121/wpa-port; cd /Users/builder/dwa121/wpa-port; WPA_BUILD_DIR=build sh scripts/test-wpa.sh' < artifacts/wpa-source-transfer.tgz
scripts/vm-ssh.sh 'cd /Users/builder/dwa121/wpa-port; tar -czf - build/libdwa-wpa2.a build/station-test build/wpa2-test build/data-test vendor/wpa_supplicant-2.12/COPYING vendor/wpa_supplicant-2.12/README' > artifacts/DWA121-WPA2-core-panther-ppc.tgz.tmp
tar -tzf artifacts/DWA121-WPA2-core-panther-ppc.tgz.tmp >/dev/null
python3 scripts/package-wpa.py --native artifacts/DWA121-WPA2-core-panther-ppc.tgz.tmp
rm artifacts/DWA121-WPA2-core-panther-ppc.tgz.tmp
