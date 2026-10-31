#!/bin/sh
# Builds and runs the host tests of the plain-C core under ASan/UBSan. Extra args go to coretest.
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
$CC -std=c89 -pedantic -Wall -Wextra -Wno-long-long -Wdeclaration-after-statement -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -o /tmp/bwcoretest coretest.c \
    ../core/igutil.c ../core/ighttp.c ../core/bwurl.c ../core/bwcharset.c ../core/bwhtml.c \
    ../core/bwform.c ../core/bwlayout.c ../core/bwcookie.c ../core/bwico.c ../core/bwstream.c
/tmp/bwcoretest "$@"
