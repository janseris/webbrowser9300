#!/bin/sh
# Assembles core/igarm.c with the phone's GCC and runs it in the Unicorn ARM emulator against Python
# references, then counts instructions against GCC's build of the C versions.
# Needs ~/sym (the toolchain) and `pip install unicorn`.
set -e
cd "$(dirname "$0")"
. ~/sym/env.sh
T=${TMPDIR:-/tmp}/bwarm
mkdir -p $T
arm-epoc-pe-gcc -O2 -D__MARM__ -D__MARM_ARMI__ -c -o $T/igarm.o ../core/igarm.c
sed -n '/^void ig_memcpy/,/^}/p;/^void ig_memset/,/^}/p;/^unsigned int ig_strlen/,/^}/p' ../core/igutil.c > $T/cver.c
arm-epoc-pe-gcc -O2 -c -o $T/cver.o $T/cver.c
for o in igarm cver; do arm-epoc-pe-objdump -d $T/$o.o > $T/$o.dis; arm-epoc-pe-nm $T/$o.o | grep " T " > $T/$o.sym; done
python3 armtest.py $T/igarm.dis $T/igarm.sym $T/cver.dis $T/cver.sym
