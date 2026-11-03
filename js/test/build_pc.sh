#!/bin/sh
# The PC test programs for js/ (ASan + UBSan): pagetest, treetest, hosttest, fmttest into $1 (default /tmp/bwjs)
set -e
cd "$(dirname "$0")"
O=${1:-/tmp/bwjs}; mkdir -p "$O"
C=../../core
F="-O1 -g -w -DBW_CALLNAMES -fsanitize=address,undefined -fno-omit-frame-pointer -I ../mujs -I .. -I $C"
CORE="$C/igutil.c $C/ighttp.c $C/bwurl.c $C/bwcharset.c $C/bwhtml.c $C/bwform.c $C/bwlayout.c $C/bwcookie.c $C/bwico.c $C/bwstream.c"
new=0; for f in ../mujs/*.c ../mujs/*.h; do [ "$O/one.o" -nt "$f" ] || new=1; done
[ $new = 0 ] || gcc $F -c ../mujs/one.c -o "$O/one.o"
gcc $F -DBWC_HOST -c ../bwlibc.c -o "$O/bwlibc.o"
gcc $F -o "$O/pagetest" pagetest.c ../bwhost.c ../bwdom.c ../bwdomjs.c ../../core/bwurl.c ../../core/igutil.c "$O/one.o" "$O/bwlibc.o" -lm
gcc $F -o "$O/esrun" esrun.c "$O/one.o" "$O/bwlibc.o" -lm
gcc $F -o "$O/treetest" treetest.c ../bwdom.c $CORE
echo built in "$O"
