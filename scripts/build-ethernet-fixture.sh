#!/bin/sh
# VM-only synthetic provider, deliberately separate from build-ethernet.sh.
set -eu
cd "$(dirname "$0")/.."
[ "$(uname -p)" = powerpc ] && [ "$(sw_vers -productVersion)" = 10.3.9 ] || exit 1
dest=build/ethernet/EthernetTestProvider.kext/Contents
mkdir -p "$dest/MacOS"
kernel=/System/Library/Frameworks/Kernel.framework/Headers
flags="-arch ppc -mcpu=750 -static -mlong-branch -msoft-float -mno-altivec -fno-common -fno-builtin -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -I$kernel -Wall -Werror -O2"
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c tests/ethernet_provider.cpp -o build/ethernet/fixture.o
gcc $flags -c tests/ethernet_provider_module.c -o build/ethernet/fixture-module.o
ld -arch ppc -r -o "$dest/MacOS/EthernetTestProvider" build/ethernet/fixture.o build/ethernet/fixture-module.o -lkmodc++ -lcc_kext
cp tests/ethernet_provider.plist "$dest/Info.plist"
