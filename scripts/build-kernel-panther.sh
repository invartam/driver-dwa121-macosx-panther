#!/bin/sh
# Execute inside Mac OS X 10.3.9 / Xcode 1.5.
set -eu
cd "$(dirname "$0")/.."
if [ "$(uname -p)" != powerpc ] || [ "$(sw_vers -productVersion)" != 10.3.9 ]; then
    echo 'Build requires the Panther PowerPC VM.' >&2
    exit 1
fi
mkdir -p build/DWA121Diagnostic.kext/Contents/MacOS
kernel=/System/Library/Frameworks/Kernel.framework/Headers
flags="-arch ppc -mcpu=750 -static -mlong-branch -msoft-float -mno-altivec -fno-common -fno-builtin -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -I$kernel -Wall -Werror -O2"
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/DWA121Diagnostic.cpp -o build/diagnostic.o
gcc $flags -c src/rtl8188cu/identity.c -o build/identity.o
gcc $flags -c src/rtl8188cu/firmware.c -o build/firmware.o
gcc $flags -c src/rtl8188cu/radio.c -o build/radio.o
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/PassiveRX.cpp -o build/passive-rx.o
gcc $flags -c src/rtl8188cu/dma.c -o build/dma.o
gcc $flags -c src/rtl8188cu/cam.c -o build/cam.o
gcc $flags -c src/rtl8188cu/txpower.c -o build/txpower.o
gcc $flags -c src/rtl8188cu/tx.c -o build/tx.o
gcc $flags -c src/rtl8188cu/station_hw.c -o build/station-hw.o
gcc $flags -c src/control/wpa.c -o build/wpa-abi.o
gcc $flags -c src/control/association.c -o build/association-abi.o
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/AssociationControl.cpp -o build/association-control.o
gcc $flags -c src/rtl8188cu/rx.c -o build/rx.o
gcc $flags -c src/net80211/beacon.c -o build/beacon.o
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/PersistentRX.cpp -o build/persistent-rx.o
gcc $flags -c src/usb/rx_lifecycle.c -o build/rx-lifecycle.o
gcc $flags -c src/usb/tx_lifecycle.c -o build/tx-lifecycle.o
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/AsyncTX.cpp -o build/async-tx.o
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/TXProbe.cpp -o build/tx-probe.o
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/ControlClient.cpp -o build/control-client.o
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/NetworkBridge.cpp -o build/network-bridge.o
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/kext/NetworkInterface.cpp -o build/network-interface.o
gcc $flags -c src/control/network.c -o build/network.o
gcc $flags -c src/control/frame_queue.c -o build/frame-queue.o
gcc $flags -c src/control/protocol.c -o build/control-protocol.o
gcc $flags -c src/kext/module.c -o build/module.o
ld -arch ppc -r -o build/DWA121Diagnostic.kext/Contents/MacOS/DWA121Diagnostic \
    build/wpa-abi.o build/station-hw.o build/association-abi.o build/association-control.o build/diagnostic.o build/identity.o build/firmware.o build/radio.o build/dma.o build/cam.o build/txpower.o build/tx.o build/rx.o build/passive-rx.o build/persistent-rx.o build/rx-lifecycle.o build/tx-lifecycle.o build/async-tx.o build/tx-probe.o build/beacon.o build/control-client.o build/frame-queue.o build/control-protocol.o build/network.o build/network-bridge.o build/network-interface.o build/module.o -lkmodc++ -lcc_kext
cp src/kext/Info.plist build/DWA121Diagnostic.kext/Contents/Info.plist
