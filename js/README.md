# JavaScript in Browser 9300

MuJS (ES5, ISC licence, `mujs/COPYING`) from https://codeberg.org/ccxvii/mujs at 01a75ff, changed for
the phone and taught ES2015+; every change is in `mujs-browser9300.patch` and marked "Browser 9300"
in the source.

| | |
|---|---|
| `mujs/` | the engine; `one.c` builds it as one unit (`group/mujs.mmp` -> `mujs.lib`) |
| `shim/` | libc headers for it: every libc name mapped to a `bwc_` function (only this library sees them) |
| `bwdom.c`, `bwdom.h` | the page as a tree: HTML parser, writer, selectors |
| `bwdomjs.c`, `bwjs.h` | the DOM for scripts: the tree's nodes as JavaScript objects |
| `bwhost.c`, `bwhost.h` | a page's run: scripts in order, document.write, events, timers, external scripts |
| `es.js` | the engine's ES2015+ runtime: the helpers the parser's lowerings call, Symbol, Map, Set, Promise, generators' objects, the newer methods (`genes.py` -> `mujs/jses.h`, every `js_State` runs it; `//@lazy` blocks when first named) |
| `prelude.js` | the window and the DOM built on the C part, and the newer methods MuJS lacks (`genprelude.py` -> `prelude.h`; `//@lazy` blocks when first named) |
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
- ES2015+ syntax (`jsparse.c`): mostly lowered to ES5 trees at parse time. Hidden names start
  with `%` (scripts cannot name them): `%this` / `%args` (what arrow functions see, set at the top
  of the function around), `%tN` temporaries, `%arr`, `%slice`, `%assign`, `%superCall` ... (in
  `es.js`). `let` / `const` are `var`, renamed (`x%7`) in a block where they would clash with a
  name already in scope. A `(` is looked ahead (to its `)`) for `=>`, giving up quickly.
- In the compiler (`jscompile.c`): `?.` chains (a jump to the chain's end), `??`, spread (into an
  array, then `%apply` / `%construct`), computed keys, `yield`, `await`; jumps are two
  instructions (functions over 64 K instructions).
- Generators / async functions (`jsrun.c`, `jses.c`): calling one makes a generator object; `%co`
  resumes its frame: the stack slice, pc, scope and open try blocks are kept in hidden properties
  at a yield (`OP_YIELD`) and put back (the try blocks set again in the new `jsR_run` frame).
  Async functions are generators driven by `%async` (Promise), async generators by `%asyncgen`.
- Properties are kept in insertion order too (`jsproperty.c`): for-in, Object.keys and JSON in
  ES2015 order. `in` does not run getters. Regexps: a loop whose body can match nothing stops
  when an iteration matched nothing (`(a*)*`).
- ES modules (`jsparse.c` `jsP_parsemodule`, `js_loadmodule`): a module is compiled into an async
  function (`%ns`, `%meta`: top-level await); its imports first (`%imp`: the imported module's namespace, that
  module run first; `%impcheck`: one not fetched yet stops it), imported names read as
  `%mN["name"]` (live), exports getters on `%ns`. `import()` is `%dimp`. The loader is in
  `prelude.js` (`__runModule`, modules by URL); `bwhost.c` runs module scripts after the others.
- `jsbytes.c`: ArrayBuffer, typed arrays and DataView as userdata (bytes in C; elements through
  the userdata index hooks `js_setuserindex`); a view made without a length follows its buffer's
  (WebAssembly memory grows in place). `jswasm.c`: a WebAssembly interpreter (decoder, block
  table per function, one value / label / frame stack); the API is `es.js`'s WebAssembly block.
  Tests: `tests/d_typed.js`, `tests/d_wasm.js` (made by `tests/wasm/mkwasm.py`, a small
  assembler; Node's results).
- Startup (every page has its own engine): interned strings in a hash table (`jsintern.c`, was an
  AA-tree), locals compared by pointer, ASCII fast paths and a first-letter keyword filter in the
  lexer. Lazy globals (`jses.c`, `jsB_lazy`): the `//@lazy` blocks of `es.js` (Map / Set / Weak*,
  Promise and async) and `prelude.js` (fetch / XHR, URL, typed arrays and Blob, crypto, ...) are
  accessors on the global object until a script reads or sets one of their names, then compiled.
  A page's engine: 61 M instructions before, 30 M after (PC, `pagetest`, an empty page).
- Errors: "Cannot read properties of undefined (reading 'x')"; syntax errors with the column; PC
  test builds (`-DBW_CALLNAMES`) also name what was not a function.

## Checks (on a PC)

    test/build_pc.sh /tmp/bwjs                                         # pagetest, treetest, esrun (ASan, UBSan)
    /tmp/bwjs/esrun tests/*.js                                         # each script's result: = tests/expected.txt
    /tmp/bwjs/treetest page.html ...                                   # the tree's round trip: the same document for the browser
    /tmp/bwjs/pagetest page.html url [scripts-dir]                     # a page after its scripts; getscripts.py fetches its external ones
    python3 test/emu/run_page.py page.html url [script-url=file ...]   # the same in the ARM emulator (test/emu/build.sh first)

    cd test
    gcc -O1 -DBWC_HOST fmttest.c ../bwlibc.c -lm && ./a.out           # printf, strtod, maths vs glibc
    gcc -O1 -DBWC_HOST shorttest.c ../bwlibc.c -lm && ./a.out | python3 shortcheck.py   # vs Python repr
    gcc -O1 -DBWC_HOST u64test.c ../bwlibc.c -lm && ./a.out           # 64-bit division
    gcc -O1 -I ../mujs -DBWC_HOST hosttest.c ../mujs/one.c ../bwlibc.c -lm && ./a.out ../tests  # = tests/expected.txt
    emu/build.sh && python3 emu/run.py                                 # the phone build (needs ~/jseng/emu/armemu.py, unicorn)
