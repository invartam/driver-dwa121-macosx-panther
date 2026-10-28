#!/bin/sh
# Separate opt-in artifact; never replaces the v0.15.0 driver.
set -eu
cd "$(dirname "$0")/.."
test "$(uname -p)" = powerpc
test "$(sw_vers -productVersion)" = 10.3.9
out=build/airport-p02
mkdir -p "$out/DWA121AirPortP02.kext/Contents/MacOS"
kernel=/System/Library/Frameworks/Kernel.framework/Headers
flags="-arch ppc -mcpu=750 -static -mlong-branch -msoft-float -mno-altivec -fno-common -fno-builtin -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -I$kernel -Wall -Werror -O2"
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/airport/AirPortDWA121.cpp -o "$out/airport.o"
gcc $flags -c src/airport/module.c -o "$out/module.o"
ld -arch ppc -r -o "$out/DWA121AirPortP02.kext/Contents/MacOS/DWA121AirPortP02" "$out/airport.o" "$out/module.o" -lkmodc++ -lcc_kext
cp src/airport/Info.plist "$out/DWA121AirPortP02.kext/Contents/Info.plist"
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/tools/dwa121-airport.c -F/System/Library/PrivateFrameworks -framework Apple80211 -framework IOKit -framework CoreFoundation -o "$out/dwa121-airport"
