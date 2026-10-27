#!/bin/sh
# Host-side reproducible source transfer and build; no kext installation.
set -eu
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project"
mkdir -p artifacts
python3 scripts/embed-firmware.py
COPYFILE_DISABLE=1 tar --format ustar -czf artifacts/source-transfer.tgz \
    src tests firmware LICENSE scripts/build-panther.sh scripts/build-kernel-panther.sh scripts/build-wpa-core.sh vendor/wpa_supplicant-2.12 vendor/patches/hostap-2.12-panther-endian.patch
scripts/vm-ssh.sh 'set -e; mkdir -p /Users/builder/dwa121/native; tar -xzf - -C /Users/builder/dwa121/native; cd /Users/builder/dwa121/native; sh scripts/build-panther.sh' < artifacts/source-transfer.tgz
trap 'rm -f artifacts/DWA121Diagnostic-panther-ppc.tgz.tmp' EXIT HUP INT TERM
scripts/vm-ssh.sh 'cd /Users/builder/dwa121/native/build && tar -czf - DWA121Diagnostic.kext dwa121-info dwa121-check dwa121-rx dwa121-control dwa121-associate dwa121-wpa2 dwa121-connect dwa121-config dwa121-service network-test eapol-test dhcp-test station-hw-test station-test control-test wire-test identity-test firmware-test radio-test rx-test beacon-test rx-lifecycle-test tx-hardware-test tx-lifecycle-test async-tx-test LICENSE.Realtek LICENSE.hostap firmware-manifest.json NOTICE-OpenBSD.txt LICENSE.txt' > artifacts/DWA121Diagnostic-panther-ppc.tgz.tmp
tar -tzf artifacts/DWA121Diagnostic-panther-ppc.tgz.tmp > /dev/null
mv artifacts/DWA121Diagnostic-panther-ppc.tgz.tmp artifacts/DWA121Diagnostic-panther-ppc.tgz
