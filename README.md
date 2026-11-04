# Browser 9300

A web browser for the **Nokia 9300 / 9500** (Series 80 2.0, Symbian 7.0s, EKA1). Native C++ with
a plain-C core. **Everything runs on the phone**: fetching (HTTP/1.1, TLS through the patched
`ssladaptor.dll`), HTML and CSS parsing, layout, pictures, forms, cookies. No proxy, nothing
precomputed on the PC. The PC is only used during development: it serves test builds and
receives logs (`localserver\ota_server.js`).

**Download:** the [latest release](https://github.com/janseris/webbrowser9300/releases/latest) (each version's notes and its sis), or
[`bin/browser9300.sis`](bin/browser9300.sis), the latest build (its version is the first line of
Status below). Copy it to the phone and open it, or open its address in the phone's
own browser; it installs on the Nokia 9300 and 9500.

```
 bytes ──► core/bwhtml.c   tokenizer + implied end tags + a little CSS ──► BwDoc (a flat op stream:
            (C89, tested       styled text, blocks, images, form fields, anchors)
             on the PC)
 BwDoc ──► core/bwlayout.c  lines, words, baseline, text-align, margins, indents ──► BwLayout (boxes)
 BwLayout ─► CBrView        off-screen bitmap: fills, text, pictures, fields, focus, scroll bar
 CPageLoader  cookies, redirects, content types, downloads (.sis -> installer)
 CImageLoader pictures nearest the screen first, ICL decode (JPEG/PNG/GIF/BMP), RAM cache
 CHttpClient  RConnection (the open link) + TCP + CSecureSocket + keep-alive + chunked + gzip
```

## Status

- **0.54**: 0.53's comparison left out the "error: " in front of a result that is an error, so
  c_runaway (which expects "error: script ran too long") showed WRONG; both parts compared now.
- **0.53**: the JavaScript test compares a script's whole result (d_builtins' grew past the 512
  characters kept for the screen and the log, so it showed as WRONG on the phone while right).
- **0.52** (phone: 22 of 23 in the JS test, d_builtins only cut short; modwasm.html 13 OK): **a quicker JavaScript start, ES modules, and engine fixes
  found on real pages.** Every page has its own engine, and setting one up (the ES2015+ runtime
  and the page's window and DOM, both JavaScript) took 61 M instructions (PC; 18.6 M of them the
  runtime: 9.4 M now on the emulated phone). Now 34 M: interned strings in a hash table (was a
  tree, a dozen string compares per word of every script), local names compared as pointers,
  ASCII fast paths in the lexer, and the rarely needed parts (Map / Set, Promise and async, fetch
  / XMLHttpRequest, URL, typed arrays and Blob, TreeWalker, crypto, MessageChannel, the module
  loader) compiled only when a page's script first names one of their globals.
  `<script type="module">` runs (after the page is read, in order; `nomodule` scripts are now
  skipped): `import` / `export` in every form (bindings live), top-level `await`, `import.meta.url`,
  `import()`; a
  module the browser has not fetched yet is noted and fetched like a script, and the page runs
  again with it. Fixes (each from a page that failed): `lastIndex` counted bytes instead of
  characters past non-ASCII text (MDN's Fluent parser looped until out of memory); sticky (`y`)
  and dotAll (`s`) regexps; `[x].reduce(f)` threw "no initial value"; a named function
  expression called itself by name past a renamed `let` of the same name (react.dev); defining a
  read-only property again with the same value threw (Next.js); `Function.prototype.name`,
  `Symbol` / `Map` / `Set` lengths, Object functions on primitives, Unicode spaces in `trim` and
  numbers, U+2028 in strings, Promise subclasses (`@@species`): polyfill bundles tested these and
  replaced the built-ins with slower ones that broke. New in the DOM: TreeWalker / NodeIterator,
  `<template>.content`, `isEqualNode`, `hasAttributes`, `DocumentType`, more `HTML*Element`
  names, marks and measures in `performance`, `getComputedStyle` display by tag, URLs against
  non-http bases. react.dev now runs with no errors (its page used to end up blank: 8 words, now
  1236); MDN's and the Guardian's modules run.
  **WebAssembly (an experiment):** an interpreter in C (`js/mujs/jswasm.c`: the MVP with sign
  extension, saturating conversions, bulk memory and multi-value blocks; no SIMD, threads or
  BigInt) behind `WebAssembly.Module` / `Instance` / `Memory` / `Global` / `Table`,
  `instantiate`, `instantiateStreaming` and `validate`; a module's `.wasm` is fetched like a
  script (`fetch` / XHR `arraybuffer` give its bytes). Its loops run about 5 times as fast as the
  same loop in the JavaScript engine. Its memory is an `ArrayBuffer`, and ArrayBuffer, the typed
  arrays and DataView are now C too (`js/mujs/jsbytes.c`; they were JavaScript arrays): views
  share their buffer's bytes, little-endian, doubles in IEEE order on the phone's FPA too. A
  fetched `.wasm` is kept as its bytes (not converted as text); a page may now fetch 4 rounds of
  scripts (was 3: modules import modules). Test page: `localserver/jstest/modwasm.html`.
- **0.51** (not yet run on the phone): **two connections over Bluetooth.** After the page's HTML,
  its stylesheets, its scripts and its pictures each had a connection of their own: three at once,
  where GnuBox measured that a third connection only adds waiting (the link carries ~35 kB/s
  however it is shared) and each new one costs 0.6 s, plus 1.7-11 s of TLS handshake. Over
  Bluetooth (the bearer 0.50 detects) the stylesheet and script fetchers now take turns, one
  request at a time, on the page loader's own connection, which is kept alive and already through
  TLS when the page came from the same host; the pictures keep the second connection. USB and
  the other bearers keep three as before.
- **0.50** (not yet run on the phone): **a 16 KB TCP window over Bluetooth.** The connection's
  bearer is read as Maps 9300 does (a modem bearer on the BTCOMM port: Bluetooth dial-up); there
  new sockets ask for 16 KB, elsewhere (USB's LAN bearer, ...) 48 KB as before. GnuBox's NetTest
  (its test log #19) measured the same ~34 kB/s over Bluetooth with 8, 16, 32 or 64 KB windows,
  the round trip growing from 358 ms (8 KB) to 624 ms (64 KB): a big window only queued data in
  front of the link, and the page's other requests waited behind it. The log says which: 
  "connection: Bluetooth 1, TCP window 16384 B".
- **0.49** (not yet run on the phone): **WAP pages (WML).** A WML deck (`text/vnd.wap.wml`, now
  in the Accept header) is turned into an HTML page (`core/bwwml.c`) the parser and layout show:
  its cards one after another, each with its id as an anchor (a link to `#card` scrolls there);
  `<do>` and `<anchor>` as links, or the button of the card's form when their `<go>` posts;
  `<postfield>`s as the form's fields (the inputs they take `$(var)` from renamed to them, the
  constant ones hidden); a `<select>` whose options navigate (onpick) as a list of links; `$(var)`
  from `<setvar>`; the first card's `<timer>` + ontimer or onenterforward as a refresh. A `<do>`
  that only goes back is left to the browser's Back. Not done: WMLScript, the binary WMLC that
  WAP gateways sent to old phones. Test decks: `localserver/wap/index.wml`, `week.wml`.
- **0.48** (not yet run on the phone): the end of a download says "Writing the file (N KB)..."
  while the RAM buffer goes to the flash (the whole file at once: the progress looked stuck a
  few KB before the end).
- **0.47** (not yet run on the phone): a file downloaded to the temporary place (installers, to
  open at once) replaces one of the same name without a second question: in 0.45 the "Replace the
  file?" question after the download question was the likely 2 s with the transfer stopped.
- **0.46** (not yet run on the phone): **a Tests & log menu; a test sends the log by itself.**
  As in Maps 9300: the JavaScript test, the speed test, sending / showing the logs, a screenshot
  and Logging are in their own menu, and a test sends the log to the PC when it is over. A log
  over 200 KB is sent from its end (it was its start: a test's results were cut off). After the
  download question each step's time is in the log, and the history is written once the transfer
  goes on (0.45 on the phone: the body came for ~4 s while the question was up, then the reading
  stopped for 2 s before the file was written, the time unknown).
- **0.45** (run on the phone, 4 Nov: maps9300.sis 7.7 s over Bluetooth, was 10 s with the reading stopped at the question; Maps 9300 itself 5.9 s): **a download goes on while its question is up.** GnuBox
  Share's logs (the PC side of internet over Bluetooth, 3 Nov) showed every download standing still
  while the "download it?" dialog was open: 3 s without a byte for browser9300.sis in 0.43 (of
  10.5 s), ~100 KB at the link's 34 kB/s; the steady speed was the same in 0.38 and 0.43. Now the
  body is read into RAM meanwhile (512 KB at most, an eighth of the free RAM), written to the file
  on OK, dropped on Cancel; a file that came whole while the question was up is saved at once.
  The progress text is redrawn every 300 ms at most (it was every 2 KB, ~17 times a second at
  35 kB/s), leaving the CPU to the Bluetooth stack, which runs on it too.
- **0.44** (run on the phone, 4 Nov: 21 of 21 tests right in 24 s, 0.43 took 90 s; b_obj 5.3 s, c_memory 3.5 s, both faster than 0.42's 6.2 s and 5.9 s): **the JavaScript engine gets its own allocator.** On the
  phone 0.43 passed all 21 tests but ran them 3-5 times slower than 0.42 did (b_obj 19.7 s, was
  6.2 s; c_memory 29.8 s, was 5.9 s) while the emulated phone build ran the same instructions as
  0.42: User::Alloc is first fit over the whole app heap's free list, and the ES runtime's
  thousands of permanent blocks made every small allocation slower. `js/bwpool.c`: blocks up to
  512 bytes in size classes with free lists, carved from 32 KB chunks; bigger ones from the system;
  all of it back at once when the page's engine goes. The memory limit counts what the pool takes
  from the system. Emulated: the same instructions, b_obj's peak 4.8 MB (5.2 MB in 0.43).
- **0.43** (run on the phone: 21 of 21 tests right, but slower than 0.42): **JavaScript of today's sites: ES2015+ syntax, Promise,
  generators, async / await, and the newer web APIs.** Checked on the PC against Node, and in the
  ARM emulator as the phone builds it.
  - Syntax (`js/mujs/jsparse.c`, lowered to ES5 trees where it can be): `let` / `const` (as var,
    renamed in a block where they would clash), arrow functions (`this` and `arguments` of the
    function around), template literals and tagged templates, destructuring (declarations,
    parameters, assignments, for-of, catch), default and rest parameters, spread in calls, `new`,
    arrays and object literals, shorthand properties, methods, computed keys, getters / setters,
    classes (extends, super, static, fields, `#private`, static blocks, extending Error),
    `for-of`, `?.`, `??`, `**`, `??=` `||=` `&&=`, `0b` `0o` `1_000` numbers, `\u{...}`, the regexp
    flags u y s (accepted), optional catch binding, trailing commas.
  - Generators, async functions and async generators run as coroutines in the interpreter
    (`js/mujs/jsrun.c`): a yield keeps its frame (stack, scope, try blocks) in the generator object,
    so `yield` inside try / catch / finally works, and `throw()` into one. `for await`. Promise jobs
    run after each script, event and timer, in the order browsers run them.
  - Built-ins (`js/es.js`, every engine runs it): Symbol (as strings: `typeof` says symbol),
    iterators, Map, Set, WeakMap, WeakSet, Promise (all / allSettled / race / any / finally),
    queueMicrotask, Object.assign / entries / fromEntries / groupBy, Array.from (iterables) / flat
    / flatMap / at / findLast / toSorted, String padStart / replaceAll / at / codePointAt / raw,
    Math and Number additions, Reflect, Annex B (toGMTString, getYear, __defineGetter__ ...).
    Objects keep their properties in insertion order (as ES2015: integer keys first): for-in,
    Object.keys and JSON.stringify give what browsers give.
  - Web APIs (`js/prelude.js`): fetch and XMLHttpRequest (a GET is answered with what the browser
    fetched for the page, in the same rounds as external scripts: 12 a page; other methods are not
    sent), Headers / Response / Request, URL, URLSearchParams, AbortController, TextEncoder /
    TextDecoder, atob / btoa, typed arrays and DataView (as arrays), Blob / File / FormData,
    crypto.getRandomValues / randomUUID, structuredClone, customElements (upgrades the page's
    elements), MessageChannel, NodeList, CSSStyleSheet. Selectors: `:nth-child()`
    `:nth-last-child()` `:nth-of-type()` `:first-of-type` ..., `:is()` `:where()` `:has()`, `:not()`
    with lists; `:hover` and the like match nothing.
  - MuJS fixes on the way: `in` no longer runs getters (core-js polyfills failed on it), regexps
    with a loop that can match nothing (`(a*)*`) run as in browsers, jumps in functions over 64 K
    instructions (Google Tag Manager), `do ... while (x) return`, a property read on undefined
    names the property ("Cannot read properties of undefined (reading 'x')"), syntax errors give
    the column, script errors their stack.
  - Real sites on the PC with their scripts: ct24.ceskatelevize.cz (Next.js, 25 scripts),
    react.dev, theguardian.com run with no script error (they failed on syntax in 0.42); bbc.com,
    idnes.cz down to the sites' own errors.
  - Creating an engine now costs about 0.08 s on the phone (it runs the ES runtime); dom.html
    takes 41 M instructions (22.6 M in 0.42), jQuery 3 116 M.
- **0.42** (not yet run on the phone): **scripts get a real page to work on (a DOM), and external
  scripts load.** Run in the ARM emulator as the phone builds it: the same pages out as on a PC.
  - The page is a tree (`js/bwdom.c`): a tolerant HTML parser (implied end tags, html / head /
    body always there) and a writer back to HTML. Ten real pages (Wikipedia, seznam.cz, idnes.cz,
    novinky.cz, BBC, Google, Hacker News, jquery.com, craigslist, 1.3 MB the biggest) come back
    giving the browser's own parser exactly what the original does. Scripts change the tree; the
    browser's parser and layout read it written out, and stay as they are.
  - The DOM (`js/bwdomjs.c` in C, the rest in `js/prelude.js`): a node gets a JavaScript object
    only when a script reaches it (cheap on the phone: no 10 000 objects up front). Tree walking
    and edits, attributes, innerHTML / outerHTML / textContent, getElementById / ByTagName /
    ByClassName, querySelector(All) / matches / closest with a selector engine (tag, #id, .class,
    [attr ^= $= *= ~= |=], :first-child ... :not(), ' ' > + ~, lists), insertAdjacentHTML,
    cloneNode; reflected properties (id, className, href, value, checked ...), classList, dataset,
    style (written back into the style attribute), events (addEventListener, dispatchEvent,
    on... properties, bubbling), DOMContentLoaded, load and <body onload>, timers (those of the
    first 3 s, in a virtual time), IntersectionObserver (everything counts as on the screen: lazy
    pictures load), document.cookie (the browser's cookie jar, both ways), location.href = ...
    (the browser goes there, as a meta refresh), eval as a function.
  - Scripts run in document order, also the ones scripts add (document.write, appendChild);
    external ones (`<script src>`) are fetched first (512 KB each, a 1.5 MB cache), the page is on
    the screen meanwhile and is shown again with their effect; scripts they add are fetched in a
    second and third round. Inline scripts over 128 KB (page data) are not run.
  - jQuery 1.12.4 and 3.7.1 run (`localserver/jstest/jquery.html`, `jquery3.html`): selectors,
    text / html / css / hide / each / appendTo / ready / addClass / attr. MuJS needed: regular
    expressions with a literal `{` (Annex B), `(1,eval)(code)`, and a defined `1 << 31`.
  - Test pages: `localserver/jstest/dom.html` (27 checks), `jquery.html`, `jquery3.html`.
  - Most of today's big sites still fail on syntax newer than ES5 (`=>`, `let`, template strings)
    and `fetch` / `Promise` / `Symbol`: next.
- **0.41** (not yet run on the phone): page scripts only where they can change something. On the
  phone 0.40 spent 24.5 s on seznam.cz's scripts (an inline script of page data: compiling it is
  not under the time limit) and 5.2 s on google.com's, writing 414 B and nothing. Until there is a
  DOM, document.write is the only way a script changes the page, so the engine starts only when an
  inline script calls it; inline scripts over 64 KB are not run; the 8 s limit is also checked
  before each script (compiling counts). The DNS retry status is just "DNS retry".
- **0.40**: a failed name lookup (-5120, the DNS server's answer
  lost or the link just up) is tried twice more, 1.5 s apart, on a new resolver, before the page
  fails (with 0.39's Go to keeping the address).
- **0.39**: Go to keeps the address that did not load: after a page
  fails (or is stopped, or is still loading) the address bar offers the address asked for, not
  the page still on the screen, so it can be retried or corrected. 0.38 on the phone: the
  page-script tests (`localserver/jstest`) all right: 21 / 21 checks, a windows-1250 page, a
  runaway script stopped at 8 s with the page shown; ~230 ms of setup per page with scripts
  (compiling the prelude), 360 KB of heap.
- **0.38**: **pages' inline scripts run** (View > JavaScript, on by
  default, at the top of the menu; it reloads the page). Before a page is read, its `<script>`
  elements run in order on the JavaScript thread (`js/bwhost.c`) and each is replaced by what it
  wrote with `document.write`; written text with a script in it is read again, so its scripts run
  too, in a browser's order. Skipped: external scripts (`src`, not loaded yet), non-JS types (JSON,
  templates, modules), anything in comments, noscript (still shown), textarea, xmp, style. An
  error ends that script only (the log has it, with `alert` and `console.log`); 8 s and the RAM
  limit for all of a page's scripts. A page in another encoding is read as UTF-8 for them. Once
  per page: when its stylesheets arrive, the result is reused. The world they see
  (`js/prelude.js`): `window`, `location` (from the URL), `navigator` (the User-Agent sent),
  `screen` 640 x 200, `document.write` / `writeln`, `alert` / `confirm` / `console` into the log,
  timers that never fire, `localStorage` for the page; no DOM yet: `getElementById` gives null,
  created elements take any call and change nothing. Added to MuJS's ES5: `substr`, `trimStart`
  / `End`, `startsWith`, `endsWith`, `includes`, `repeat`, `padStart` / `End`, `find`,
  `findIndex`, `fill`, `Array.from` / `of`, `Object.assign` / `values` / `entries`,
  `Number.isNaN` / `isInteger` / ..., `Math.sign` / `trunc` / `log10` / ..., `escape`, `unescape`.
  Checked on a PC (`js/test/pagetest.c`, `page1.html`) and in the ARM emulator as the phone builds
  it (`js/test/emu/run_page.py`: the same page out); about 30 ms a page for the setup.
- **0.37**: 0.36 crashed (KERN-EXEC 3) when the JavaScript test wrote
  its log: `l.Append(at ? _L8("   ...") : aLabel)`, the GCC 2.9 ternary that slices a `TPtrC8`
  temporary mixed with a `const TDesC8&` (0.28's Wikipedia crash). An if / else now.
- **0.36**: 0.35 ran all 16 JavaScript tests on the phone, 14 right;
  the other two (c_math, c_numbers) had results longer than the 160 characters the test kept,
  so the cut result could not match: now 512, and the log has every whole result (and the
  expected one when wrong). Menus: the things being tested on top (Tools: JavaScript test, Speed
  test; View: Desktop pages). Phone times: b_obj 6.1 s, b_loop 1.3 s, b_json 1.25 s, b_str 0.9 s,
  c_memory 5.6 s; the runaway script stopped at its 2 s.
- **0.35**: 0.34's JavaScript test stopped at `c_math` with the
  engine's thread panicking USER-EXEC 3 (the browser went on): EUSER's soft float raises an
  exception for 1/0, 0/0 or an overflow, where IEEE (and GCC's own, in the emulator) just gives
  Infinity / NaN, and the thread had no handler. It has one now, which does nothing, so the
  operation returns the IEEE value. On the phone 0.34 got the first 9 right: b_inline 47 ms,
  b_json 1.25 s, b_loop 1.3 s, b_obj 6.1 s, b_str 0.9 s.
- **0.34**: **the JavaScript engine is in**, not yet used by pages:
  **Tools > JavaScript test** (`about:js`, `src/jsplat.cpp`) runs 16 scripts on the phone (the five
  benchmarks, numbers, Math, strings, regular expressions, errors, closures, dates, objects / JSON,
  memory, deep nesting, a runaway loop) and checks each result against Node's, with its time and heap.
  - **MuJS** (`js/mujs`, ES5) with no writable static data (the AA-tree and string sentinels and
    the undefined value live in the `js_State`), built as `mujs.lib` (`group/mujs.mmp`) with its own
    include path: `js/shim`'s libc headers map every libc name to a `bwc_` function, so nothing
    clashes with Symbian's headers.
  - **Its C library** (`js/bwlibc.c`, C, tested on the PC against glibc): strings, `setjmp` in ARM
    assembly, the IEEE helpers, the six GCC 64-bit / unsigned helpers EUSER lacks (`__udivdi3`,
    `__umoddi3`, `__cmpdi2`, `__floatdidf`, `__fixunsdfdi`, `__fixunsdfsi`), and a `vsnprintf`
    with exact `%f %e %g` (the double expanded to all its decimal digits). The platform half
    (`src/jsplat.cpp`): User:: heap, Math::, the clock, the time zone from TLocale.
  - **Number fixes MuJS needed** (stock MuJS gets these wrong on a PC too): a correctly rounded
    `strtod` (it read 5e-324 as 0 and 1.7976931348623157e308 an ulp off); the shortest-closest
    digits for Number to String (Grisu2 printed 0.1 + 0.2 as 0.30000000000000007); toFixed /
    toExponential / toPrecision round half up as JavaScript says ((12.5).toFixed(0) is 13), and
    toPrecision switches to exponents only below 1e-6. Checked: 716 000 formatting / parsing cases
    against glibc, 300 000 doubles against Python's repr, 0 differences.
  - **Safety**: scripts run on their own thread with a 256 KB stack (a regular expression needs
    ~35 KB, each nested call ~150 bytes, 1024 deep at most), sharing the heap while the app waits;
    a crash there ends only that thread. The engine's heap stops at the free RAM less 512 KB (or
    16 MB): "out of memory" stops the script, not the browser. A time limit (the clock checked every
    100 000 instructions) stops a script, and then its own catch / finally blocks are skipped. Error
    stack traces keep the 24 innermost calls (a 1000-deep one took 15 MB).
  - Checked in the ARM emulator as the phone builds it (`js/test/emu`: GCC 2.9 -O2, the shim,
    the six helpers): all 16 right.
  - Not in MuJS (ES5): `substr`, `startsWith`, `includes`, `Object.assign`, `Array.from`, `Map`,
    `Promise`, arrow functions, `let` / `const`, classes. Next (0.35): inline scripts and
    `document.write` while a page is parsed, a little `window` / `document` / `location`, the
    common missing methods, View > JavaScript.
- **0.33**: the address bar is the phone's own combo box again, with its
  own buttons, as in 0.28: 0.29's subclass (`EBrCtAddressCombo`, a custom control to keep Go on top)
  panicked BAFL 4 (a resource read past its end) when Go to was pressed. Native controls stay native.
- **0.32**: **Tools > Speed test** (`about:speed`, `src/speed.cpp`): the
  phone's own time for a double add / subtract / multiply / divide, int <-> double, compare, an int
  divide, and a 4 KB copy (our assembly vs Mem::Copy), each looped until it takes 0.3 s less the same
  loop without it; shown as a page (ns, cycles at 150 MHz, next to GCC's own soft float in the
  emulator) and in the log. For the JavaScript engine choice (`webbrowser9300/jsbench`): every JS
  number is a double, and the SDK has only EUSER's import library, so its speed can only be
  measured on the phone.
- **0.31**: pictures decoded by Maps 9300's own decoders
  (`src/pngdec.cpp`, `src/jpegdec.cpp`, `src/jpegasm.c`, copied from `maps9300` 1.19; the IDCT is
  FFmpeg's simple IDCT via mp4player9300, LGPL-2.1-or-later). In Maps the phone's ICL took a median
  0.41 s a 256 px PNG and 171 ms a JPEG (one in ten 3 s or more); these ~30-50 ms and ~110-200 ms,
  straight into RGB565, a JPEG at half size from its lowest frequencies when that still covers the
  page's size. They run in a thread of their own (`br9300dec<n>`, the shared heap, EPriorityLess)
  and the picture is copied into the bitmap when it ends; GIFs, progressive JPEGs, interlaced
  PNGs, anything that fails: the phone's ICL as before (its own thread). Checked on the PC against
  PIL: Wikipedia's RGBA PNG 41.4 dB (blended over white), JPEG 4:2:0 37.9 dB, 4:4:4 41.2 dB, half
  size 28.8 dB, progressive refused.
  - **RAM**: the caches leave 512 KB free (was 2.5-3 MB), as Maps 9300's 3D view: one app runs at a
    time. Free RAM counts what is free inside the browser's heap too.
- **0.30**: faster, and the things asked for on 2026-11-02.
  - **Parsing ~38 % fewer instructions** (Wikipedia's page with its 854 CSS rules: 46.6 M to
    28.9 M in the ARM emulator, `test/emu/`: the phone's GCC 2.9 code in Unicorn, per function;
    the same output to the byte on every test page). GCC 2.9 -O2 doesn't inline: `is_ws` (a call
    a character, 16 %) is a macro; `has_class` compares while scanning and skips words with one
    compare a character; `word_is` checks the literal's length first (a macro with `sizeof`);
    a rule's index bucket is hashed once, not at every rebuild after a `<style>`;
    `contains_ci("var(")` compares only where the first letter matches; `invisible` has a fast
    path below U+00AD.
  - **Scrolling**: when only the position changed (keys, the pointer at an edge), the page part of
    the buffer is moved (`CopyRect`) and only the uncovered strip drawn; any other change (focus,
    the link under the pointer, a picture, the find box) draws everything as before. The status
    and title redraw only the bars.
  - **Pictures decode in a thread of their own** (`EOptionAlwaysThread`): a big PNG kept the app
    busy ~2.5 s and keys and scrolling waited.
  - **Progress** for the long steps, drawn before each starts: "Unzipping N KB...", "Reading the
    page (N KB)...", "Applying the stylesheets...", "Laying out...", and "decoding WxH" in the
    pictures' line.
  - **Not downloaded**: SVG / WebP / AVIF pictures (the phone can't show them; their alt text is
    shown). A `srcset` with only 1.5x / 2x copies keeps `src` (the 1x one: the phone's screen is
    1x; Wikipedia's photo 250 px, not the 180 KB 500 px one).
  - **The pointer reaches the top**: in full screen the pointer's area stayed the normal client
    area (below where the title bar was); it is set to the whole view. At the top / bottom edge
    the page scrolls at once (no waiting ticks), 6, 9, 12 ... px a tick, at most 60.
  - **View > Desktop pages**: a desktop Firefox User-Agent (with Browser9300/0.N at its end), so
    sites send their desktop layout; the page reloads. Saved in the settings (`desktop=`).
  - **The log** appends what is new (it rewrote up to 60 KB at each trace: ~0.7 s a line with
    logging on) and keeps its file server session.
- **0.29** (crashed on the phone, BAFL 4; undone in 0.33): the address bar's buttons: **Go** on top (as OK is in every
  Series 80 dialog), **History** (the pages visited, as Down) second, Cancel at the bottom. The
  phone's combo box put its Change button over the first button whenever it had the focus;
  the field is now `CBrCombo` (a custom control, `EBrCtAddressCombo`) whose `FocusChanged` takes
  that button away again.
- **0.28**: fixes the KERN-EXEC 3 on `en.m.wikipedia.org/wiki/Nokia_9300`
  (and any page where the stack happened to hold the wrong value). 0.27's log ended at
  "icon: address set" with the heap intact, which left one line: the side bar's host name was
  taken with `TPtrC8 rest = s >= 0 ? aUrl.Mid(s + 2) : aUrl;`. A `TPtrC8` temporary and a
  `const TDesC8&` meet in a sliced `TDesC8` copy that keeps the length but not the pointer, so
  the host was read through stack garbage. Now `rest.Set()` in an `if`. The other ternaries over
  descriptors were checked (both sides `TPtrC8`, safe). The extra traces of 0.27 are gone; its
  `IgTraceHeap` (a step logged after `RHeap::Check`) stays for the next hunt.
- **0.27**: finding the KERN-EXEC 3 on `en.m.wikipedia.org/wiki/Nokia_9300`.
  0.26's log stopped after "pictures cleared", before "icon set". The page, its stylesheets, its
  favicon and its headers all pass the PC tests (ASan, every allocation made to fail in turn), so
  with Tools > Logging on this build logs every step from there to the layout and checks the heap
  (RHeap::Check) at "page received", "parsed and styled", "pictures cleared" and "address kept":
  "heap check: X" without "X" after it means the heap was already corrupt.
- **0.26**: a faster start. Work that isn't needed for the start page
  now waits until it is:
  - **The four HTTP clients** (page, pictures, stylesheets, the helper) open their socket server
    sessions on their first request, not at start. Four `RSocketServ::Connect`s, the first of
    which may even start ESOCK, aren't needed for the internal start page.
  - **`cookies.txt`, `history.txt` and `downloads.txt`** are read and parsed when first needed:
    cookies before the first web request; the history for the address bar, the History page or
    the first visit; the downloads for the Downloads page or the first download. A file that
    was never read is never saved, so it can't be overwritten with an empty list.
  - **The log** is rotated only with Logging on (two file renames otherwise).
  - **The CPU readout**'s search for the kernel's idle thread and its first update come one
    second after start.
  - The startup time is logged ("startup: N ms") and shown in Page info, so the next steps can be
    measured on the phone.
- **0.25**: the USER 23 crash on downloads fixed.
  - **Cause**: `CHttpClient::SinkWrite` (from Instagram 9300) took a `TPtr8` to its buffer,
    flushed the full buffer to the file, then appended through the old `TPtr8`. That pointer still
    had the full length, so the Append overflowed (USER 23, an 8-bit descriptor exceeding its
    maximum length). It only happened when a file was bigger than the buffer, which is sized
    from the free RAM: right after start the buffer is bigger than the file, later in a session
    (caches full) it isn't. That's why a fresh start "fixed" it. The pointer is now taken after
    any flush.
  - The internal pages (Downloads, History, Bookmarks, Start) now size their buffers for the
    worst escaping ('&' becomes 5 bytes, UTF-8 up to 3), so links full of `&` can't overflow them.
- **0.24**: the downloads list; replacing a file is asked.
  - **Bookmarks > Downloads (Ctrl+J)** opens `about:downloads`: every file downloaded
    (newest first, up to 100, kept in `downloads.txt`). For each: its name, folder, size or
    "deleted", and **[open]**, **[delete]** and **[download again]**.
  - **Delete the installers**: one link deletes all the downloaded `.sis` / `.sisx` files
    (they're only needed to install) and says how many and how many KB. Another link removes
    the entries whose files are gone.
  - **A name that's taken**: "name is there already. Replace it?". Yes overwrites it, which is
    the usual answer for an installer. No keeps both ("name (2).ext").
- **0.23**: Down in the address bar.
  - The phone's virtual cursor (the pointer) takes the arrow keys while it's on, so Down never
    reached the address field. While a text dialog is open (the address bar, Find, form
    fields) it's now set to `ESuspended`, the state Symbian provides for edit fields that
    need the arrows, and it's switched on again when the dialog closes.
  - Down opens the list even when it's empty; the phone then says there's nothing in the
    history. (Pages are added once they've loaded; nothing was added while 0.13-0.16
    crashed.) Tab opens it too when it isn't empty.
  - The log records "address: N pages in the list" and each key the dialog gets.
  - **The history also gets downloads** (the link, titled "Download: file name"). It's saved
    right after each new entry, not only on exit, so a crash doesn't lose it.
- **0.22**: **a new 3D app icon**: a shaded globe (oceans and continents lit from the upper
  left, a graticule that follows the sphere, a specular highlight, a blue rim light) with a
  tilted golden orbit ring passing behind and in front of it. It's drawn by
  `tools/make_icon.py` (original, numpy, 8x supersampled; the 25x20 size gets a wider ring).
  The masks are black on the icon, as this phone wants; edges are blended toward the Desk's
  light background, because the mask is 1-bit.
- **0.21**: history across restarts; the update path.
  - **The history is kept**: the pages visited, newest first, each once, up to 200, in
    `C:\System\Data\browser9300\history.txt` (URL TAB title, UTF-8). It's saved when the
    browser goes to the background or exits, in one write. The History page (with "Clear the
    history") and the address bar's list use it. Back / Forward stay per run, as on a computer.
  - Tools > Update from the PC fetches `/browser9300.sis` (the `.sis` in the local server's
    folder itself, not in a `browser9300\` subfolder).
- **0.20**: downloads bigger than the RAM; the app icon fixed.
  - **Every accepted download now streams to its file.** Before, chunked replies (the common
    kind for generated files) were collected in RAM, and gzip replies would have been saved
    compressed. Now the body goes through `bw_dechunk` (chunked framing removed as it
    arrives, any split) and, for gzip, a raw-inflate stream (the header via
    `bw_gzip_header_len`, then 32 KB at a time) into the file. The RAM used doesn't depend on
    the file's size. Both decoders are in `core/bwstream.c`, unit-tested under ASan (every
    split, byte by byte, junk).
  - The prompt shows each place's free space ("Phone: My files (12.3 MB free)"). A file
    bigger than the free space there asks "Not enough space ... Download anyway?" first.
  - **The app icon**: its masks were the wrong way round for this phone, which draws
    where the mask is black (as Instagram 9300 found for `BitBltMasked`). Desk showed a white
    rectangle with the globe cut out. `aif/icon25_mask.bmp` and `icon64_mask.bmp` are now
    black on the icon, white around it. Desk may keep the old icon until the phone restarts.
- **0.19**: asks before downloading a file.
  - When a reply turns out to be a file (not HTML / text / XML / a picture, or sent as
    `Content-Disposition: attachment`, or a `.jad`), the transfer **pauses at its headers**
    (`MHttpObserver::HttpHeadersArrived` returns EFalse; nothing more is read and the
    watchdog waits). A dialog shows:
    - **File**: its type and size ("application/zip, 1.2 MB", or "size unknown");
    - **From**: the site;
    - **Name**: editable (from `Content-Disposition` or the URL);
    - **Save to**: Phone: My files (or Documents, whichever exists), Memory card (E:) when
      one is in, or Temporary (to open / install). `.sis`, `.sisx`, `.jar` and `.jad` default
      to Temporary.
  - **OK** (or Enter) resumes the transfer straight into that file (`CHttpClient::ResumeL`; the
    body is written as it arrives, not held in RAM). An existing file isn't overwritten: it
    becomes "name (2).ext". **Cancel** drops the transfer.
  - When it's done: "Downloaded. Open it?" (a `.sis` goes to the installer, as before);
    otherwise "Saved: path".
- **0.18**: the address bar is a combo box.
  - Its list holds the pages visited (newest first, each once, up to 40; no `about:` pages),
    using Series 80's own `CEikComboBox` with its history popout. **Down** opens the list; pick
    with Up/Down and Enter, which puts the address in the field; **Enter** again goes there.
    Typing, select-all of the current address and search for non-addresses work as before.
- **0.17**: .ico files are never given to the phone's ICO decoder.
  - **The likely crash**: since 0.14 every web page fetched its site's `/favicon.ico` right
    after the download, and handed it to Symbian's own ICO decoder. Modern favicons often hold
    PNG images (since Windows Vista), which a 2004 decoder misreads; a crash inside it takes the
    app down with KERN-EXEC 3, on every page, just after the download.
  - **Now `core/bwico.c` reads .ico files**: it picks the image nearest 16 px. A PNG inside
    goes to the PNG decoder; a bitmap one (1/4/8/24/32 bpp, palette, AND mask or alpha) is
    decoded there and blended over the bar's blue for the icon (white elsewhere). It's
    bounds-checked and unit-tested under ASan, including every truncation and 4,000 corrupted
    copies.
  - The 0.16 trace stays (with Logging on). With View > Load pictures off, no icon is loaded
    either, so that's a quick test if it still crashes.
- **0.16**: a trace for the crash after a page arrives (0.15 still
  crashed, so it wasn't only the stack). With Tools > Logging on, every step after a download
  writes a "trace: ..." line and saves the log at once. The steps: show the page, parsed and
  styled, pictures cleared, icon set, laid out, the first draws, title kept, history trimmed,
  each picture's fetch / decode / done, the CPU probe's start. After a crash, start the
  browser again and open Tools > Show the previous log: the last trace line is the last step
  that worked.
- **0.15**: the crash on every page (KERN-EXEC 3) fixed, very likely
  a stack overflow.
  - **Cause**: an app's main thread on EKA1 has only ~8 KB of stack. A page arrived deep in the
    network callbacks (HTTP client 324 B, page loader 1,088 B, ...) and was parsed, laid out and
    *drawn* right there. 0.11-0.14 made the drawing deeper: the side bar 660 B, its text
    wrapping 348 B, the icon row. Frames measured from `sub sp` in the ARM objects.
  - **Fix**: drawing is asynchronous. `Render()` only asks; `DoRender` runs from a callback
    right after the current event, from a shallow stack, and once for several requests. A
    downloaded page, and the re-parse when its stylesheets arrive, are also shown from their
    own callbacks instead of inside the network code.
  - **Tools > Show the log / Show the previous log** show the log's last 48 KB as a page, so it
    can be read on the phone without a cable. After a crash, start the browser again and open
    the previous log. It's written only with Tools > Logging on. It's now also saved right
    after a page arrives and after it's parsed, so it shows how far a crash got.
- **0.14**: the page's icon.
  - **Favicon**: shown in front of the site's name at the top of the side bar, or in front of the
    title in the top bar (16 px).
    - The parser picks the best `<link rel=icon>` (`shortcut icon`, `apple-touch-icon`): PNG / GIF
      over ICO, sizes near 16-32 px over big ones, never SVG (the phone can't decode it).
      Unit-tested.
    - Without one, the site's `/favicon.ico` is tried; if the phone can't decode it, there's
      no icon.
    - It's fetched first, before the page's pictures, and isn't counted in "Pictures: ...".
      With pictures off, it isn't loaded either.
- **0.13**: CPU without CPU times, the bar's place and width, the
  pointer's top.
  - **CPU works without the kernel's CPU times** (the phone showed "?"). Once a second, a thread
    at the lowest priority counts how far it gets in about 0.1 s. It only runs when nothing else
    wants the CPU, so the most it ever got is an idle phone, and less means busy:
    busy = 100% - count / best count. It costs about 10% of one second and never slows anything
    else down. The app's own share is shown only when the phone keeps thread CPU times.
    The first readings can be low until it has seen an idle moment.
  - **View > Bar: at the top / side, narrow (80 px) / side (100 px) / side, wide (130 px)**,
    independent of full screen (`bar=`; Full screen now only hides the side buttons).
  - **The pointer's top edge**: the cursor stops lower than the 12 px strip. The browser now
    remembers the highest point the cursor has reached and treats resting there (if it's in the
    top quarter) as the top edge. Push the cursor up as far as it goes and wait.
  - The download progress no longer shows a percentage ("12 / 40 KB" says it).
- **0.12**: CPU and RAM shown, and more RAM used.
  - **View > CPU and RAM** (on by default): three lines at the bottom of the side bar (or in
    the top bar's row), updated once a second while the browser is in front. Only the bar is
    redrawn, not the page.
    - "CPU 35%, app 20%": the whole phone (100% less the kernel idle thread's share of the
      last second) and the browser's own thread (`RThread::GetCpuTime`). If the kernel keeps
      no CPU times, it shows "?"; the log records the errors.
    - "RAM free 18.2 MB".
    - "App 6.1 MB of N": the browser's heap allocations plus its pictures, of the phone's total
      RAM as the system reports it (`HAL EMemoryRAM`, in whole MB).
  - **Caches grow with the free RAM** instead of fixed sizes.
    - Pages kept for Back / Forward: was 6 pages / 1.5 MB, now as many as fit, up to 16 MB
      (4 MB per page).
    - Stylesheets: was 900 KB, now up to 4 MB.
    - The receive buffer for big downloads: up to 24 MB (was 12).

    Each leaves 2.5 MB free for the phone. Pictures keep their 3 MB reserve, so they give way
    first. The heap may grow to 32 MB (was 24). After pages are dropped, the heap is
    compressed, so the RAM goes back to the phone.
- **0.11**: progress in more detail.
  - **The status has a line for each part:**
    - the page: its stage ("Connecting to the server...", "TLS handshake..."), then
      "12 / 40 KB, 30%, 9 KB/s" while it arrives (the size, when the server sends it), then
      "Page: 34 KB, 1.2 s";
    - "Styles: 2 to load" while stylesheets are on the way;
    - "Pictures: 5, 3 to load, 1 failed" (the pictures near the screen are asked for, so the
      count grows as you scroll).

    The side bar shows them as lines, the top bar in one row separated by "·". Progress updates
    every 2 KB (was 4).
  - **Fixed: "1 pictures to load" stayed** after the last picture arrived (the empty "done"
    message was dropped). Pictures now report their counts, and the line shows the final tally.
- **0.10**: search from the address bar; Enter goes.
  - **What isn't an address is searched for** on DuckDuckGo Lite, which works without JavaScript
    (`https://lite.duckduckgo.com/lite/?q=...`, UTF-8, percent-encoded). An address is
    something with a scheme and `//` (or `about:`, `file:`, `data:`, `mailto:`, `tel:`),
    `localhost`, an IPv4 address, or a host with a dot and a top-level part of 2+ letters
    (`seznam.cz`, `idnes.cz/zpravy`). Words with spaces, a single word (`seznam`), `3.14` and
    `note: buy milk` are searched for (`bw_input_is_address`, `bw_search_url`, unit-tested).
  - **Enter is OK** in the address bar, Find and one-line form fields: the field takes no line
    breaks any more. A textarea still takes Enter as a new line.
  - **Edge scrolling up works**: the cursor stops a few pixels short of the top of the screen,
    so the strip that scrolls up is now 12 px tall (including the side bar's top) instead of 3.
- **0.9**: full screen with a side bar.
  - **Full screen is the default.** The side buttons are hidden (the Menu key still works).
    The bar that was across the top is now a 100 px column at the right, where the buttons
    were. It shows the page title at the top, the address of the link under the pointer (or
    the selected one) under it, and the loading status at the bottom. The page gets the whole
    200 px of height, so it is less of a wide, short strip.
  - **View > Full screen** switches back to the older layout (side buttons, bar at the top) and
    again. It's stored as `full=`, so older settings files that said `fullscreen=0` start full
    screen once.
  - A click on the side bar's top half pages up, on its bottom half pages down.
- **0.8**: Opera-style Up/Down, and the pointer fixed.
  - **Up/Down move between links and fields** the way the 9300's built-in Opera does: the next
    (previous) one if it is on the screen, otherwise the page scrolls a step. Long text between
    links still reads by pressing Down. **Ctrl+Up/Down** scroll a step without moving the
    focus; Shift+Up/Down still move a page. The Ctrl jump to the top / end is gone (Home/End,
    if a keyboard sends them, and Page > Top / Bottom remain).
  - **The pointer really starts on now.** Settings saved by 0.4 or earlier said `mouse=0`, so
    the 0.5 default never took effect. The setting is now stored as `pointer=`, and the cursor
    is switched on again whenever the browser comes to the front. The log records the cursor's
    state ("cursor: ...") and the arrow keys that reach the page ("key ... mods ..."), to show
    whether the cursor takes the arrows.
  - **+ / − zoom** (− is Shift+plus on the 9300) through the text sizes; Ctrl+0 goes back to
    normal. With a text field focused, they're typed into it.
  - **Typing on the page no longer opens the address bar** (Ctrl+L, the Go to button or the
    menu do). A letter goes into the focused text field, or does nothing.
- **0.7**: zoom and the original size.
  - **View > Original size** lays the page out 1024 px wide, as on a computer: CSS margins and
    paddings as given (not halved), list indents at 40 px, pictures and tables up to their real
    width. Left/Right (Shift: a screen) scroll sideways, with a bar at the bottom. Tab, Find and
    Enter bring the link or match into view sideways too. Resting the pointer on the left or
    right edge scrolls sideways. Fitted to the screen (the default), Left/Right still move
    between links.
  - **Ctrl+Plus / Ctrl+Minus / Ctrl+0** zoom like a desktop browser, through the three text
    sizes. The page reflows to the width, so zooming never needs sideways scrolling.
- **0.6**: speed.
  - **A quadratic copy fixed**: the element numbering for `:hover` (0.5) copied a whole array on
    every start tag. On the 490 KB test page it took 81 % of the work. The page now needs
    336 M instructions instead of 1,665 M (5x fewer).
  - **ARM assembly** (`core/igarm.c`, phone only) for `ig_memcpy`, `ig_memset` and `ig_strlen`,
    which every buffer, array growth and string copy goes through:
    - memcpy and memset do 32 bytes per LDM/STM, with a shift-merge path for unaligned sources;
    - strlen checks a word at a time.
    Instruction counts in the emulator, against GCC 2.9's build of the C loops: memcpy 4 KB
    527 vs 20,484 (unaligned source 6,164), memset 4 KB 536 vs 16,388, strlen 200 bytes 269 vs
    809. In cycles, expect roughly 10x for big aligned copies (LDM/STM take about a cycle a word).
    `test/armtest.sh` runs the assembled code in the Unicorn ARM emulator against Python
    references: 4,000 random sizes and alignments with guard bytes, overlaps, and register and
    stack checks.
- **0.5**: the mouse pointer and desktop-style keys.
  - **Hover**: the joystick's pointer is on by default (View > Mouse pointer). The cursor is
    polled about 8 times a second, because move events don't always arrive. The link under it
    is underlined (pictures get a frame) and its address shows in the header. When the pointer
    rests on an element for about 0.35 s, the page is styled again with `:hover` applied to it
    and its ancestors, so dropdown menus open and links change colour. Typed text and ticks are
    kept. This only happens on pages that have `:hover` rules and that parse and lay out in under
    0.9 s.
  - **Edge scrolling**: rest the pointer on the header or the bottom edge and the page scrolls,
    faster the longer it stays.
  - **Keys as on a computer**: Up/Down scroll a step, PgUp/PgDn and Space/Shift+Space a page,
    Home/End the top/end, and Tab/Shift+Tab move between links and fields. Enter opens the
    focused one even when it is off the screen. Space ticks a focused box or presses a button.
    A letter typed with a text field focused goes into it. The 9300 has no
    Home/End/PgUp/PgDn/Alt keys, so it also has: Shift+Up/Down for a page, Ctrl+Up/Down for
    top/end, Left/Right for the previous/next link, and Ctrl+Left/Right for back/forward.
  - Core: `:hover` selectors, element numbers on ops/boxes, `bw_doc_parse_hover`,
    `bw_doc_hover_chain`, `bw_doc_copy_form_state` and `bw_layout_hit_box` (unit-tested).
- **0.4**: more HTML5 and CSS, all on the phone.
  - **Address bar only**: the start page has no search boxes; typing a letter or digit on a
    page opens the address bar with it, and the bar opens with the page's address selected.
  - **CSS box model**: `margin`, `padding`, `border` (shorthands, sides, width / style / color),
    `width` / `max-width` (px, %, em) with `margin: auto` centring, block backgrounds over the
    padding. Desktop lengths are halved for the 640 x 200 screen and capped (vertical margins
    16 px, horizontal 40, padding 10 / 16). `<fieldset>` has its frame.
  - **Cascade**: `!important` (after the normal declarations, the style attribute's last).
  - **Selectors**: attribute selectors `[a] [a=v] [a~=v] [a^=v] [a$=v] [a*=v] [a|=v]` (on the
    element itself), `:first-child`, `:nth-child(odd | even | An+B)`, `:not(.x | #x | tag | [a])`,
    `:root`, `:link` / `:visited`. Rules with `:hover`, `:focus`, `:last-child`, `+`, `~` are
    still dropped rather than guessed.
  - **Values**: all 148 CSS colour names, `hsl()` / `hsla()`, `#rgba` / `#rrggbbaa`, `rgb(r g b /
    a)`; `var(--x, fallback)` with the custom properties of `:root` / `html` / `body` (a theme
    class like `.dark` does not override them).
  - **Lists**: `list-style(-type)`: none, disc, circle, square, decimal, lower/upper-alpha,
    lower/upper-roman; `<ol type=a|A|i|I>`.
  - **Flow**: `float: left/right` and the items of `display: flex / grid` rows flow side by side
    with a gap (horizontal menus, card rows); `flex-direction: column` stacks them.
  - **Text**: `::before` / `::after` with `content` strings, escapes and `attr()` (icon-font
    glyphs are left out), `<q>` quotation marks, `text-transform`, `white-space: nowrap`
    (`<nobr>`, `<td nowrap>`), inline backgrounds (`<mark>`, labels, badges).
  - **Pictures**: `srcset` (the smallest candidate at least 480 px wide), `<picture><source>`
    (the first JPEG / PNG / GIF one whose `media` fits; WebP / AVIF skipped), CSS `width` /
    `height` on pictures, the `<noscript>` copy of a lazy-loaded picture not shown twice.
  - **Media queries**: a light screen without hover, a coarse pointer, 1 pixel per pixel,
    landscape (dark-mode, hover and 2x rules are skipped).
  - **HTML5**: closed `<dialog>` hidden, `<object>` fallback content shown, image map `<area>`
    links listed under the picture, icon-only links and buttons named by `aria-label` / `title`,
    `<label for>` and labels around a field (pressing the label works the field), `disabled` /
    `readonly` fields, textarea placeholders, table `width` attributes (columns and
    `width=100%` tables), soft hyphens / zero-width characters / emoji not drawn as boxes.

- **0.3**:
  - **Tables with columns**: rows and cells are now in the op stream (TABLE / ROW / CELL
    start and end); the layout measures every cell's narrowest and widest form (min / max
    content, long words capped at 150 px since they are broken anyway), sizes the columns as
    browsers do (the widest form if it fits, else min plus a share of the rest), honours
    `colspan`, `rowspan` (the spanned columns stay free in the rows below; the cell's height
    counts for its first row), `cellpadding`, `border` (cell and table frames), `bgcolor` / CSS backgrounds of
    tables, rows and cells, `align` of cells and of the table, `<th>` centred and bold, a
    `<caption>` above. Up to 10 columns and 4 nested levels; a table that does not fit even
    at its narrowest stacks its rows as before (cells one after another). CSS `display:
    table / table-row / table-cell` builds the same structure; `display: block` on cells
    (responsive tables) stacks them.
  - A table row is one line of the layout, and the screen is now drawn and clicked line by
    line, so a short sidebar next to a long column is drawn too.
  - **View > Encoding**: automatic, UTF-8, windows-1250, ISO-8859-2, windows-1252 for pages
    that name the wrong one; the page is shown again from its kept copy (no new download),
    until the next page.
  - **Page > Copy the address** (Ctrl+C): the focused link's address, else the page's, to the
    clipboard (paste it in Messaging, Notes...).
  - Measured on the PC: 390 KB of random nested tables (58,000 ops, 18,000 boxes) parse and lay
    out in 24 ms.

- **0.2**:
  - **External stylesheets**: `<link rel=stylesheet>` (with its `media`) and `@import` in
    `<style>` are loaded on the phone after the page is on the screen, one at a time over a
    connection of their own (cookies, gzip, redirects, 30 s watchdog, up to 400 KB each, 8 per
    page); when they are in, the page is laid out again at the same place. They stay in a
    900 KB cache, so the next page of the same site gets them at once (the page is then shown
    styled right away). View > Load stylesheets turns it off. Page info shows how many applied.
  - **Descendant and child selectors** (`.menu a`, `nav > ul li`), matched exactly against the
    open elements (their tag, id and classes are kept on the element stack); rules apply in
    specificity order (ids, classes, tags), then source order. A selector with `+`, `~`,
    pseudo-classes or attribute selectors is still dropped. Rules are indexed by their
    subject's id / class / tag: 2,700 rules on a 1,500-article page cost 11 ms on the PC (it was
    63 ms checking every rule).
  - **Font sizes** are computed in pixels down the tree (`%`, `em` relative to the parent,
    `rem`, `px`, `pt`, keywords, `<font size>`, headings 2 / 1.5 / 1.17 em) and then mapped to
    the 4 phone sizes (under 14 px small, 14-17 normal, 18-25 large, 26+ extra large), so the
    `body{font-size:62.5%} p{font-size:1.4em}` pattern gives normal text.
  - Links take the page's link colour and underline unless a matching rule sets them.
  - **Find in page** (Page > Find, Ctrl+F; Ctrl+N next): ignores case and accents ("prilis"
    finds "příliš"), wraps around, the match is shown on yellow.
  - **Password fields** are typed in a secret editor (the text is not shown).
  - Ctrl+F is now Find; Forward is in the Page menu.

- **0.1** (the first version):
  - HTML: tolerant parsing (implied `</p>`, `</li>`, `</td>`, `</option>`..., stray end tags,
    raw text in script/style/textarea/title, comments, CDATA), 378 named entities + numeric,
    UTF-8 / windows-1250 / ISO-8859-2 / windows-1252 (HTTP header, `<meta charset>`, BOM, or a
    guess: valid UTF-8, else windows-1250), `<base>`, `<meta http-equiv=refresh>` (followed when
    the delay is up to 15 s), `<title>`, frames and iframes as links, `<noscript>` shown (there
    is no JavaScript), `<video>/<audio src>` as links.
  - CSS: `style=""`, `<style>` blocks and `<style media>`; selectors `tag`, `.class`, `#id`
    and their compounds (`p.note`, `.a.b`), groups; `@media` evaluated for a 640 px screen
    (`min-width` / `max-width`), `@supports` read, other at-rules skipped. Properties: display
    (none / block / inline / list-item / table-cell / table-row), color, background(-color) of
    blocks, font-weight, font-style, font-size (px / pt / em / % / keywords to 4 sizes),
    font-family monospace, text-align, text-decoration, white-space: pre, visibility. Hidden
    "screen reader only" text (off-screen, clip, 1x1 px) is dropped. Selectors with
    combinators (`a b`, `a > b`) or pseudo-classes are ignored rather than guessed.
    External stylesheets (`<link rel=stylesheet>`) are not loaded yet.
  - Legacy presentation: `<font color size>`, `<center>`, `align=`, `bgcolor`, `<body text
    link bgcolor>`. Text that would be invisible on its background gets black or white.
  - Layout: word wrap (long words are broken), mixed fonts on a common baseline, 4 sizes x
    bold x italic x mono, headings, lists with bullets / numbers (`<ol start>`, `<li value>`),
    blockquote / dd indents, `<pre>`, `<hr>`, tables as rows of cells with a gap (fits 640 px),
    pictures scaled to the width (width/height attributes honoured), anchors for `#links`.
    (0.3: tables get real columns, see above.)
  - Forms: text, password, hidden, checkbox, radio, select, textarea, submit, image, reset,
    `<button>`; GET and POST (urlencoded, in the page's charset); a form with one text field
    is sent as soon as the field is edited (like Enter in a search box).
  - Pictures: loaded one at a time over their own kept-open connection, nearest the screen
    first (up to a screen above and two below), decoded at the ICL 1/2..1/8 reduction that
    covers the page width, transparency on white, kept while 3 MB of RAM stays free.
    `data:` pictures and WebP/AVIF/SVG show their alt text.
  - Navigation: history of 40 pages; as many of them as the free RAM allows (up to 16 MB, 0.12)
    are kept in RAM, so Back and Forward show them at once at the same scroll position. Bookmarks (file), start page
    with search boxes (DuckDuckGo Lite, Wikipedia), history page, cookies (persistent ones
    kept in a file), downloads of other files saved to `C:\System\Temp` and opened (a `.sis`
    goes to the installer).
  - Development: Tools > Update from the PC, Send the log, Send the previous log, Send a
    screenshot (BMP), the PC server address; Page > Page info (sizes, download / parse /
    layout times, counts, free RAM).

## Install

1. The TLS 1.2 `ssladaptor.dll` must be in `C:\System\Libs` (it is on this phone) for https.
2. Copy `bin\browser9300.sis` to `C:\Users\Jan\phoneapk\localserver\browser9300.sis` (the
   server folder itself, no subfolder) and start `node ota_server.js` there.
3. First time: open `http://192.168.137.1:8000/browser9300.sis` in the phone's own browser (or
   install with PC Suite). Later versions: **Tools > Update from the PC** (0.21 and later fetch
   `/browser9300.sis`; earlier versions looked in a `browser9300/` subfolder).
4. Start **Browser 9300** from Desk.

## Keys

| | |
|---|---|
| Up / Down | next / previous link or field when it is on the screen, otherwise scroll a step |
| Ctrl+Up / Ctrl+Down | scroll a step |
| Shift+Up / Shift+Down, Space / Shift+Space, PgUp / PgDn | page up / down |
| Home / End (if the keyboard has them) | top / end of the page |
| Tab / Shift+Tab, Right / Left | next / previous link or field (scrolls to it) |
| Ctrl+Left / Ctrl+Right | back / forward |
| Left / Right at View > Original size | scroll sideways (Shift: a screen) |
| Ctrl+Plus / Ctrl+Minus / Ctrl+0 | zoom: bigger / smaller / normal text (also + / −) |
| Space on a box or button | tick it / press it |
| a letter | into the focused text field, else nothing |
| + / − (Shift+plus) | zoom in / out (not in a text field) |
| Enter | open the focused link; edit the field; tick the box; choose from the list |
| Esc, Backspace | stop loading; otherwise back |
| Ctrl+L | go to an address (side button **Go to**) |
| Ctrl+B / Ctrl+D / Ctrl+H / Ctrl+J | bookmarks / add this page / history / downloads |
| Ctrl+R | reload (Forward: Page menu) |
| Ctrl+F / Ctrl+N | find in page / find the next one |
| Ctrl+I | page info |
| Ctrl+C | copy the focused link's address (else the page's) |
| Ctrl+G | pictures on / off |
| Ctrl+S | the start page |

The bar at the side (full screen, the default) or at the top (View > Full screen off, with the
side buttons) shows the page title, the address of the link under the pointer or the selected
one, and the status: the request stages, "34 KB, 1.2 s" when a page is done, pictures still to
load; View > Mouse pointer (on by default) turns
the phone's cursor on or off. The link under the cursor is underlined and its address shows in
the header; resting on an element applies `:hover` styles. Resting on the header or the bottom
edge scrolls. A click opens a link; a click on the right edge jumps there.

## Files on the phone

`C:\System\Data\browser9300\`: `settings.txt`, `bookmarks.txt` and `history.txt` (URL TAB title per line, UTF-8),
`cookies.txt`, `log.txt` and `log_prev.txt` (the previous run). Downloads: `C:\System\Temp\`.

## Build

```sh
bash s80-build-kit/install.sh            # once: the toolchain into ~/sym
./build.sh                               # -> bin/browser9300.sis, prints "Data Size: 00000000"
./test/run.sh                            # host unit tests of core/ under ASan/UBSan
./test/run.sh test/pages/news.html       # a page laid out at 500 px, printed as text lines
./test/run.sh -fuzz test/pages/news.html # 3000 corrupted copies parsed and laid out
./test/armtest.sh                        # core/igarm.c (ARM assembly) in the Unicorn emulator
                                         # (needs `pip install unicorn`)
```

App UID `0x0F5A9310`. The version is `KBrVersion` in `inc/browser.h` (the build puts it into the
package; bump it and the user agent for every delivered build).

`core/` is C89 with no libc and no writable static data; memory goes through
`ig_malloc`/`ig_free` (`src/util.cpp` -> `User::Alloc`). On the phone `ig_memcpy` / `ig_memset` / `ig_strlen` are the ARM assembly in `core/igarm.c` (`IG_ASM`, set in `igcore.h` for ARM builds; `-DIG_NO_ASM` turns it off). `igutil.c` / `ighttp.c` are Instagram
9300's, `src/net.cpp` is its HTTP client (minus the video side file), `src/images.cpp` is derived
from its image store.

## Measured on the PC (to compare with the phone)

A 490 KB synthetic page (1500 articles, 16,500 ops, 9,000 boxes) parses and lays out in 12 ms on
the PC; expect roughly 1-3 s on the 150 MHz ARM9 (word measuring through the phone's fonts
adds to that). Page info shows the real parse / layout times on the phone.

## Next

- Measure on the phone: parse / layout times on real pages, memory, picture speed.
- Text selection and copy (Instagram's `textsel.cpp`).
- Table cell `width` attributes / CSS widths, vertical-align, rowspan heights.
- `@import` inside external stylesheets, `:first-child`-style pseudo-classes, `+` / `~`.
- Faster picture decoding with Mapy's own PNG/JPEG decoders.
