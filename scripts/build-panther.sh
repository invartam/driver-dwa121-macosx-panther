#!/bin/sh
# Execute inside Mac OS X 10.3.9 / Xcode 1.5.
set -eu
cd "$(dirname "$0")/.."
sh scripts/build-kernel-panther.sh
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/tools/dwa121-info.c -framework IOKit -framework CoreFoundation -o build/dwa121-info
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/tools/dwa121-check.c -framework CoreFoundation -o build/dwa121-check
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/tools/dwa121-rx.c -framework IOKit -framework CoreFoundation -o build/dwa121-rx
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/tools/dwa121-control.c src/net80211/station.c src/net80211/beacon.c src/rtl8188cu/tx.c -framework IOKit -framework CoreFoundation -o build/dwa121-control
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/tools/dwa121-associate.c src/net80211/station.c src/net80211/beacon.c src/net80211/eapol.c -framework IOKit -framework CoreFoundation -o build/dwa121-associate
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/net80211/eapol.c tests/eapol_test.c -o build/eapol-test
./build/eapol-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/net80211/dhcp_probe.c tests/dhcp_test.c -o build/dhcp-test
./build/dhcp-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/rtl8188cu/station_hw.c src/rtl8188cu/txpower.c src/rtl8188cu/cam.c src/control/association.c tests/station_hw_test.c -o build/station-hw-test
./build/station-hw-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/net80211/station.c src/net80211/beacon.c tests/station_test.c -o build/station-test
./build/station-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/control/frame_queue.c src/control/protocol.c src/rtl8188cu/rx.c tests/control_test.c -o build/control-test
./build/control-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 tests/wire_test.c -o build/wire-test
./build/wire-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/rtl8188cu/identity.c tests/identity_test.c -o build/identity-test
./build/identity-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/rtl8188cu/identity.c src/rtl8188cu/firmware.c tests/firmware_test.c -o build/firmware-test
./build/firmware-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/rtl8188cu/identity.c src/rtl8188cu/firmware.c src/rtl8188cu/radio.c tests/radio_test.c -o build/radio-test
./build/radio-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/rtl8188cu/dma.c src/rtl8188cu/rx.c tests/rx_test.c -o build/rx-test
./build/rx-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/net80211/beacon.c src/rtl8188cu/rx.c tests/beacon_test.c -o build/beacon-test
./build/beacon-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/usb/rx_lifecycle.c tests/rx_lifecycle_test.c -o build/rx-lifecycle-test
./build/rx-lifecycle-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/rtl8188cu/cam.c src/rtl8188cu/txpower.c src/rtl8188cu/tx.c src/rtl8188cu/station_hw.c src/control/wpa.c tests/tx_hardware_test.c -o build/tx-hardware-test
./build/tx-hardware-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/usb/tx_lifecycle.c tests/tx_lifecycle_test.c -o build/tx-lifecycle-test
./build/tx-lifecycle-test
# Compile the actual kernel transport against deterministic test doubles, in
# user space. This does not replace the separately compiled real IOKit kext.
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 -c src/usb/tx_lifecycle.c -o build/tx-life-shim.o
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 -c src/rtl8188cu/tx.c -o build/tx-shim.o
g++ -arch ppc -mcpu=750 -Wall -Werror -O2 -Itests/panther_shim src/kext/AsyncTX.cpp tests/async_tx_test.cpp build/tx-life-shim.o build/tx-shim.o -o build/async-tx-test
./build/async-tx-test
cp firmware/LICENSE.Realtek build/LICENSE.Realtek
cp firmware/manifest.json build/firmware-manifest.json
cp src/rtl8188cu/NOTICE build/NOTICE-OpenBSD.txt
cp LICENSE build/LICENSE.txt
WPA_BUILD_DIR=build/wpa sh scripts/build-wpa-core.sh
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 -DDWA_WPA2_PROBE -Ivendor/wpa_supplicant-2.12/src src/tools/dwa121-associate.c src/rtl8188cu/tx.c build/wpa/libdwa-wpa2.a -framework IOKit -framework CoreFoundation -o build/dwa121-wpa2
cp vendor/wpa_supplicant-2.12/COPYING build/LICENSE.hostap
printf 'Built bounded association/WPA2 diagnostic probes; data port remains closed.\n'

gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/control/network.c tests/network_test.c -o build/network-test
./build/network-test

gcc -arch ppc -mcpu=750 -Wall -Werror -O2 -DDWA_WPA2_PROBE -DDWA_NETWORK_BUILD src/config/config.c -Ivendor/wpa_supplicant-2.12/src src/tools/dwa121-associate.c src/rtl8188cu/tx.c build/wpa/libdwa-wpa2.a -framework IOKit -framework CoreFoundation -o build/dwa121-connect

gcc -arch ppc -mcpu=750 -Wall -Werror -O2 -Ivendor/wpa_supplicant-2.12/src src/tools/dwa121-config.c src/config/config.c build/wpa/libdwa-wpa2.a -o build/dwa121-config

gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/tools/dwa121-service.c src/config/config.c -o build/dwa121-service
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/config/config.c tests/config_test.c -o build/config-test
./build/config-test
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/config/config.c tests/config_file_test.c -o build/config-file-test
