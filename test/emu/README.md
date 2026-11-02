# The core in the ARM emulator

`build.sh` compiles the core as the phone does (GCC 2.9 armi -O2, from `symbian-build/native9300/emu`:
`armemu.py`, `build_emu.sh`) with `drv.c` (a bump heap, the entry points). Then

    python3 prof.py out page.html a.css b.css      # parse + layout: instructions, per function, hot blocks
    python3 callers.py out page.html a.css FUNC    # who calls FUNC how often

(the phone runs ~17 ns an instruction when the data is in the cache; compare versions, not ms).
Needs `pip install unicorn` and the EKA1 toolchain on PATH.
