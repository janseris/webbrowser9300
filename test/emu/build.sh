#!/bin/sh
# the core as the phone builds it, into out/ for prof.py
cd "$(dirname "$0")" && sh build_emu.sh out -I../../core drv.c ../../core/igutil.c ../../core/igarm.c ../../core/bwurl.c ../../core/bwcharset.c ../../core/bwhtml.c ../../core/bwform.c ../../core/bwlayout.c >/dev/null
