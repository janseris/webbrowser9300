# JavaScript in Browser 9300

MuJS (ES5, ISC licence, `mujs/COPYING`) from https://codeberg.org/ccxvii/mujs at 01a75ff, changed for
the phone; every change is in `mujs-browser9300.patch` and marked "Browser 9300" in the source.

| | |
|---|---|
| `mujs/` | the engine; `one.c` builds it as one unit (`group/mujs.mmp` -> `mujs.lib`) |
| `shim/` | libc headers for it: every libc name mapped to a `bwc_` function (only this library sees them) |
| `bwdom.c`, `bwdom.h` | the page as a tree: HTML parser, writer, selectors |
| `bwdomjs.c`, `bwjs.h` | the DOM for scripts: the tree's nodes as JavaScript objects |
| `bwhost.c`, `bwhost.h` | a page's run: scripts in order, document.write, events, timers, external scripts |
| `prelude.js` | the window and the DOM built on the C part, and the newer methods MuJS lacks (`genprelude.py` -> `prelude.h`) |
| `bwlibc.c` | the C half of that library (strings, setjmp, IEEE helpers, exact printf / strtod / shortest digits, GCC helpers EUSER lacks) |
| `../src/jsplat.cpp` | the platform half (heap, Math::, clock, time zone), the 256 KB-stack thread, about:js |
| `tests/` | the about:js scripts, `expected.txt` (Node 22's results), `gen.py` -> `../src/jstests.h` |
| `test/` | PC checks (below) and `emu/`: the phone build in the ARM emulator |

## Changes to MuJS

- No writable static data (EKA1): the property and string sentinels and the undefined value are
  allocated in the `js_State`; tables are `const`.
- GCC 2.9: the 64-bit loop test in `jsdtoa.c`'s `normalize_boundary` (see
  `symbian-build/native9300/README.md`), FPA double word order.
- Numbers: `js_strtod` -> `bwc_strtod` (correctly rounded), Number to String -> `bwc_shortest`
  (shortest, closest), toFixed / toExponential / toPrecision -> `bwc_jsfmt` (half up, spec rules).
- `js_setlimithook`: the host is asked for more instructions when the limit runs out (a clock
  check); a stopped script skips its own catch / finally (`killed`).
- Stack traces: the 24 innermost calls. `Date.now` and the time zone from the phone.

## Checks (on a PC)

    test/build_pc.sh /tmp/bwjs                                         # pagetest, treetest (ASan, UBSan)
    /tmp/bwjs/treetest page.html ...                                   # the tree's round trip: the same document for the browser
    /tmp/bwjs/pagetest page.html url [scripts-dir]                     # a page after its scripts; getscripts.py fetches its external ones
    python3 test/emu/run_page.py page.html url [script-url=file ...]   # the same in the ARM emulator (test/emu/build.sh first)

    cd test
    gcc -O1 -DBWC_HOST fmttest.c ../bwlibc.c -lm && ./a.out           # printf, strtod, maths vs glibc
    gcc -O1 -DBWC_HOST shorttest.c ../bwlibc.c -lm && ./a.out | python3 shortcheck.py   # vs Python repr
    gcc -O1 -DBWC_HOST u64test.c ../bwlibc.c -lm && ./a.out           # 64-bit division
    gcc -O1 -I ../mujs -DBWC_HOST hosttest.c ../mujs/one.c ../bwlibc.c -lm && ./a.out ../tests  # = tests/expected.txt
    emu/build.sh && python3 emu/run.py                                 # the phone build (needs ~/jseng/emu/armemu.py, unicorn)
