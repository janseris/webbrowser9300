# Browser 9300

A web browser for the **Nokia 9300 / 9500** (Series 80 2.0, Symbian 7.0s, EKA1). Native C++ with
a plain-C core. **Everything runs on the phone**: fetching (HTTP/1.1, TLS through the patched
`ssladaptor.dll`), HTML and CSS parsing, layout, pictures, forms, cookies. No proxy, nothing
precomputed on the PC. The PC is only used during development: it serves test builds and
receives logs (`localserver\ota_server.js`).

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

- **0.4** (not yet run on the phone): more HTML5 and CSS, all on the phone.
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

- **0.3** (not yet run on the phone):
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

- **0.2** (not yet run on the phone):
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

- **0.1** (first version, not yet run on the phone):
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
  - Navigation: history of 40 pages; the last 6 pages (up to 1.5 MB) are kept in RAM, so Back
    and Forward show them at once at the same scroll position. Bookmarks (file), start page
    with search boxes (DuckDuckGo Lite, Wikipedia), history page, cookies (persistent ones
    kept in a file), downloads of other files saved to `C:\System\Temp` and opened (a `.sis`
    goes to the installer).
  - Development: Tools > Update from the PC, Send the log, Send the previous log, Send a
    screenshot (BMP), the PC server address; Page > Page info (sizes, download / parse /
    layout times, counts, free RAM).

## Install

1. The TLS 1.2 `ssladaptor.dll` must be in `C:\System\Libs` (it is on this phone) for https.
2. Copy `bin\browser9300.sis` to `C:\Users\Jan\phoneapk\localserver\browser9300\browser9300.sis`
   and start `node ota_server.js` there.
3. First time: open `http://192.168.137.1:8000/browser9300/browser9300.sis` in the phone's own
   browser (or install with PC Suite). Later versions: **Tools > Update from the PC**.
4. Start **Browser 9300** from Desk.

## Keys

| | |
|---|---|
| Up / Down | next / previous link or field when it is on the screen, otherwise scroll |
| Left / Right, Tab / Shift+Tab | previous / next link or field (scrolls to it) |
| Shift+Up / Shift+Down, Space / Shift+Space | page up / down |
| Ctrl+Up / Ctrl+Down | top / end of the page |
| Enter | open the link; edit the field; tick the box; choose from the list |
| Esc, Backspace | stop loading; otherwise back |
| Ctrl+L | go to an address (side button **Go to**) |
| Ctrl+B / Ctrl+D / Ctrl+H | bookmarks / add this page / history |
| Ctrl+R | reload (Forward: Page menu) |
| Ctrl+F / Ctrl+N | find in page / find the next one |
| Ctrl+I | page info |
| Ctrl+C | copy the focused link's address (else the page's) |
| Ctrl+G | pictures on / off |
| Ctrl+S | the start page |

The header shows the page title (or the focused link's address) and the status on the right:
the request stages, "34 KB, 1.2 s" when a page is done, pictures still to load. View > Full
screen hides the side buttons (the menu still works); View > Mouse pointer turns the phone's
virtual cursor on (a click opens a link; a click in the header pages up, at the bottom pages
down, on the right edge jumps there).

## Files on the phone

`C:\System\Data\browser9300\`: `settings.txt`, `bookmarks.txt` (URL TAB title per line, UTF-8),
`cookies.txt`, `log.txt` and `log_prev.txt` (the previous run). Downloads: `C:\System\Temp\`.

## Build

```sh
bash s80-build-kit/install.sh            # once: the toolchain into ~/sym
./build.sh                               # -> bin/browser9300.sis, prints "Data Size: 00000000"
./test/run.sh                            # host unit tests of core/ under ASan/UBSan
./test/run.sh test/pages/news.html       # a page laid out at 500 px, printed as text lines
./test/run.sh -fuzz test/pages/news.html # 3000 corrupted copies parsed and laid out
```

App UID `0x0F5A9310`. The version is `KBrVersion` in `inc/browser.h` (the build puts it into the
package; bump it and the user agent for every delivered build).

`core/` is C89 with no libc and no writable static data; memory goes through
`ig_malloc`/`ig_free` (`src/util.cpp` -> `User::Alloc`). `igutil.c` / `ighttp.c` are Instagram
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
