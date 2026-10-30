#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p artifacts
cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/rtl8188cu/identity.c tests/identity_test.c -o artifacts/identity-test-host
./artifacts/identity-test-host
cc -std=c89 -pedantic -Wall -Wextra -Werror tests/wire_test.c -o artifacts/wire-test-host
./artifacts/wire-test-host

python3 scripts/embed-firmware.py
cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/rtl8188cu/identity.c src/rtl8188cu/firmware.c tests/firmware_test.c -o artifacts/firmware-test-host
./artifacts/firmware-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/rtl8188cu/identity.c src/rtl8188cu/firmware.c src/rtl8188cu/radio.c tests/radio_test.c -o artifacts/radio-test-host
./artifacts/radio-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/rtl8188cu/dma.c src/rtl8188cu/rx.c tests/rx_test.c -o artifacts/rx-test-host
./artifacts/rx-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/net80211/beacon.c src/rtl8188cu/rx.c tests/beacon_test.c -o artifacts/beacon-test-host
./artifacts/beacon-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/usb/rx_lifecycle.c tests/rx_lifecycle_test.c -o artifacts/rx-lifecycle-test-host
./artifacts/rx-lifecycle-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/rtl8188cu/cam.c src/rtl8188cu/txpower.c src/rtl8188cu/tx.c src/rtl8188cu/station_hw.c src/control/wpa.c tests/tx_hardware_test.c -o artifacts/tx-hardware-test-host
./artifacts/tx-hardware-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/control/frame_queue.c src/control/protocol.c src/rtl8188cu/rx.c tests/control_test.c -o artifacts/control-test-host
./artifacts/control-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/usb/tx_lifecycle.c tests/tx_lifecycle_test.c -o artifacts/tx-lifecycle-test-host
./artifacts/tx-lifecycle-test-host
cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -c src/usb/tx_lifecycle.c -o artifacts/tx-life-shim.o
cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -c src/rtl8188cu/tx.c -o artifacts/tx-shim.o
c++ -std=c++98 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/panther_shim \
    src/kext/AsyncTX.cpp tests/async_tx_test.cpp artifacts/tx-life-shim.o artifacts/tx-shim.o -o artifacts/async-tx-test-host
./artifacts/async-tx-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/rtl8188cu/station_hw.c src/rtl8188cu/txpower.c src/rtl8188cu/cam.c src/control/association.c tests/station_hw_test.c -o artifacts/station-hw-test-host
./artifacts/station-hw-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/net80211/station.c src/net80211/beacon.c tests/station_test.c -o artifacts/station-test-host
./artifacts/station-test-host
cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/net80211/eapol.c tests/eapol_test.c -o artifacts/eapol-test-host
./artifacts/eapol-test-host
cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    src/net80211/dhcp_probe.c tests/dhcp_test.c -o artifacts/dhcp-test-host
./artifacts/dhcp-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g src/control/network.c tests/network_test.c -o artifacts/network-test-host
./artifacts/network-test-host
cc -std=c89 -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -g src/config/config.c tests/config_test.c -o artifacts/config-test-host
./artifacts/config-test-host

cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g src/net80211/scan.c tests/scan_test.c -o artifacts/scan-test-host
./artifacts/scan-test-host
