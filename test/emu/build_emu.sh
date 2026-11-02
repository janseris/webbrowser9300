#!/bin/sh
# Builds C/C++ sources as the phone's code (GCC 2.9 armi -O2, the S80 SDK's flags) into an image the
# Unicorn emulator runs (armemu.py): emu.exe and emu.sym. Your driver (from drv_template.cpp) gives
# the extern "C" entry points; no Symbian libraries (no EUSER: plain C/C++ only).
#   PATH: arm-epoc-pe-gcc (the EKA1 toolchain, e.g. ~/sym/gcc539/bin)
#   ./build_emu.sh out_dir -I../inc drv.cpp ../src/a.cpp ../src/b.c ...
set -e
OUT=$1; shift
mkdir -p "$OUT"
INC=""; SRC=""
for a in "$@"; do case "$a" in -I*|-D*) INC="$INC $a";; *) SRC="$SRC $a";; esac; done
CXX="-O2 -fno-exceptions -fno-rtti -D__MARM__ -D__MARM_ARMI__"
CC="-O2 -D__MARM__ -D__MARM_ARMI__"
OBJS=""
for s in $SRC; do
    o="$OUT/$(basename "$s" | sed 's/\.[^.]*$//').o"
    case "$s" in *.c) arm-epoc-pe-gcc $CC $INC -c "$s" -o "$o";; *) arm-epoc-pe-gcc $CXX $INC -c "$s" -o "$o";; esac
    OBJS="$OBJS $o"
done
LG=$(dirname $(arm-epoc-pe-gcc -print-libgcc-file-name))/libgcc.a
arm-epoc-pe-ld -e emu_init --image-base 0x10000 -o "$OUT/emu.exe" $OBJS $LG
arm-epoc-pe-nm "$OUT/emu.exe" > "$OUT/emu.sym"
echo "$OUT/emu.exe, $OUT/emu.sym"
