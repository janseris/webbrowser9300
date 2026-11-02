#!/bin/sh
# MuJS + js/bwlibc.c as the phone's code (GCC 2.9 armi -O2, -nostdinc, js/shim) into an image for
# ~/jseng/emu/armemu.py (Maps 9300's Unicorn runner). Then: python3 run.py
set -e
cd "$(dirname "$0")"
J=../..
export PATH=$HOME/sym/gcc539/bin:$HOME/sym/wrap:$PATH
CC="arm-epoc-pe-gcc -O2 -nostdinc -D__MARM__ -D__MARM_ARMI__ -D__SYMBIAN32__ -I$J/shim -I$J/mujs -I$J/../core"
LG=$(dirname $(arm-epoc-pe-gcc -print-libgcc-file-name))/libgcc.a
mkdir -p o
$CC -c drv.c -o o/drv.o
$CC -c $J/bwlibc.c -o o/bwlibc.o
$CC -c $J/../core/igarm.c -o o/igarm.o
$CC -c $J/mujs/one.c -o o/mujs.o 2>&1 | grep -v "warning\|In file\|In function\|from " || true
arm-epoc-pe-ld -e emu_init --image-base 0x10000 -o o/mj.exe o/drv.o o/mujs.o o/bwlibc.o o/igarm.o $LG
arm-epoc-pe-nm o/mj.exe > o/mj.sym
arm-epoc-pe-nm o/mujs.o o/bwlibc.o | grep -E " [dDbBC] " && { echo "writable data!"; exit 1; } || true
echo built
