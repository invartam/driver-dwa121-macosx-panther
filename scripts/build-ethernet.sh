#!/bin/sh
# Native build of the optional offline network attachment. Does not install it.
set -eu
cd "$(dirname "$0")/.."
[ "$(uname -p)" = powerpc ] && [ "$(sw_vers -productVersion)" = 10.3.9 ] || exit 1
dest=build/ethernet
mkdir -p "$dest/DWA121Ethernet.kext/Contents/MacOS"
kernel=/System/Library/Frameworks/Kernel.framework/Headers
flags="-arch ppc -mcpu=750 -static -mlong-branch -msoft-float -mno-altivec -fno-common -fno-builtin -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -I$kernel -Wall -Werror -O2"
g++ $flags -fapple-kext -fno-exceptions -fno-rtti -c src/netkext/DWA121Ethernet.cpp -o "$dest/ethernet.o"
gcc $flags -c src/netkext/module.c -o "$dest/module.o"
ld -arch ppc -r -o "$dest/DWA121Ethernet.kext/Contents/MacOS/DWA121Ethernet" "$dest/ethernet.o" "$dest/module.o" -lkmodc++ -lcc_kext
cp src/netkext/Info.plist "$dest/DWA121Ethernet.kext/Contents/Info.plist"
gcc -arch ppc -mcpu=750 -Wall -Werror -O2 src/tools/dwa121-netcheck.c -framework IOKit -framework CoreFoundation -o "$dest/dwa121-netcheck"
echo 'Built offline Ethernet attachment; activation remains refused.'
