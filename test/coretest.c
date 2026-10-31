/* Browser 9300 - host tests of the plain C core (run.sh: gcc + ASan/UBSan).
 * Usage: coretest            the unit tests
 *        coretest FILE.html  lays the file out at 500 px and prints the lines (a text "screenshot")
 *        coretest -fuzz FILE  parses and lays out 3000 corrupted copies */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/bwcore.h"

void* ig_malloc(unsigned int n) { return malloc(n ? n : 1); }
void ig_free(void* p) { free(p); }

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int m_width(void* ctx, int font, const unsigned short* s, int n)
{
    int sz = BW_FONT_SIZE(font), w = 5 + sz + ((font & BW_FONT_BOLD) ? 1 : 0);
    (void)ctx; (void)s;
    return n * w;
}
static int m_height(void* ctx, int font) { (void)ctx; return 12 + 3 * BW_FONT_SIZE(font); }
static int m_ascent(void* ctx, int font) { (void)ctx; return 9 + 2 * BW_FONT_SIZE(font); }
static BwMetrics kM = { 0, m_width, m_height, m_ascent };

static void check_url(const char* base, const char* rel, const char* want)
{
    char* r = bw_url_resolve(base, rel, (unsigned int)strlen(rel));
    if (!r || strcmp(r, want) != 0) { printf("FAIL url %s + %s = %s (want %s)\n", base, rel, r ? r : "NULL", want); fails++; }
    free(r);
}

static void to8(const unsigned short* s, unsigned int n, char* out, unsigned int max)
{
    unsigned int k = ig_utf16_to_utf8(s, n, out, max - 1);
    out[k] = 0;
}

static void test_urls(void)
{
    const char* b = "http://a/b/c/d;p?q";
    check_url(b, "g", "http://a/b/c/g");
    check_url(b, "./g", "http://a/b/c/g");
    check_url(b, "g/", "http://a/b/c/g/");
    check_url(b, "/g", "http://a/g");
    check_url(b, "//g", "http://g/");
    check_url(b, "?y", "http://a/b/c/d;p?y");
    check_url(b, "g?y", "http://a/b/c/g?y");
    check_url(b, "#s", "http://a/b/c/d;p?q#s");
    check_url(b, "..", "http://a/b/");
    check_url(b, "../g", "http://a/b/g");
    check_url(b, "../../g", "http://a/g");
    check_url(b, "../../../g", "http://a/g");
    check_url(b, "", "http://a/b/c/d;p?q");
    check_url(b, "  g h ", "http://a/b/c/g%20h");
    check_url("http://Example.COM", "HTTPS://Foo.org/X", "https://foo.org/X");
    check_url("https://x.cz/a/", "mailto:a@b", "mailto:a@b");
    check_url("http://x.cz", "a.html", "http://x.cz/a.html");
    check_url("http://x.cz/p?q=1", "?q=2", "http://x.cz/p?q=2");
    {
        char host[64];
        int port, sec;
        const char* path;
        unsigned int pl;
        CHECK(bw_url_split("https://User@WWW.Seznam.cz:8443/a/b?c#d", host, 64, &port, &sec, &path, &pl));
        CHECK(strcmp(host, "www.seznam.cz") == 0 && port == 8443 && sec == 1 && pl == 6 && memcmp(path, "/a/b?c", 6) == 0);
        CHECK(bw_url_split("http://x.cz", host, 64, &port, &sec, &path, &pl) && pl == 1 && path[0] == '/' && port == 80);
        CHECK(!bw_url_split("ftp://x", host, 64, &port, &sec, &path, &pl));
    }
    {
        char* u = bw_url_from_input(" seznam.cz ");
        CHECK(u && strcmp(u, "http://seznam.cz/") == 0);
        free(u);
        u = bw_url_from_input("192.168.137.1:8000/x");
        CHECK(u && strcmp(u, "http://192.168.137.1:8000/x") == 0);
        free(u);
    }
    CHECK(bw_url_same_page("http://a/b", "http://a/b#x"));
    CHECK(!bw_url_same_page("http://a/b", "http://a/c#x"));
    CHECK(!bw_url_same_page("http://a/b", "http://a/b"));
}

static void test_charsets(void)
{
    CHECK(bw_charset_from_name("Windows-1250", 12) == BW_CS_1250);
    CHECK(bw_charset_from_name("ISO-8859-2", 10) == BW_CS_8859_2);
    CHECK(bw_charset_char(BW_CS_1250, 0x9A) == 0x0161);      /* š */
    CHECK(bw_charset_char(BW_CS_8859_2, 0xB9) == 0x0161);
    CHECK(bw_charset_byte(BW_CS_1250, 0x0161) == 0x9A);
    CHECK(bw_charset_sniff("text/html; charset=\"iso-8859-2\"", "", 0) == BW_CS_8859_2);
    {
        const char* h = "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1250\">";
        CHECK(bw_charset_sniff(0, h, (unsigned int)strlen(h)) == BW_CS_1250);
    }
    {
        const char* h = "<meta charset=utf-8>";
        CHECK(bw_charset_sniff(0, h, (unsigned int)strlen(h)) == BW_CS_UTF8);
    }
}

/* the text of the doc's ops, with | for block boundaries */
static void doc_text(const BwDoc* d, char* out, unsigned int max)
{
    unsigned int i, o = 0;
    out[0] = 0;
    for (i = 0; i < d->opCount && o + 8 < max; i++) {
        const BwOp* op = &d->ops[i];
        if (op->type == BW_OP_TEXT || op->type == BW_OP_MARKER) {
            char t[2048];
            to8(d->pool + op->off, op->len, t, sizeof(t));
            o += (unsigned int)sprintf(out + o, "%s", t);
        } else if (op->type == BW_OP_BLOCK_START || op->type == BW_OP_BLOCK_END || op->type == BW_OP_BR ||
                   op->type == BW_OP_ROW_START || op->type == BW_OP_ROW_END || op->type == BW_OP_TABLE_START || op->type == BW_OP_TABLE_END) {
            if (!o || out[o - 1] != '|') { out[o++] = '|'; out[o] = 0; }
        } else if (op->type == BW_OP_IMAGE) o += (unsigned int)sprintf(out + o, "[img]");
        else if (op->type == BW_OP_CONTROL) o += (unsigned int)sprintf(out + o, "[ctl%d]", d->controls[op->ref].type);
    }
    while (o && out[o - 1] == '|') out[--o] = 0;
}

static BwDoc* parse(const char* html)
{
    return bw_doc_parse(html, (unsigned int)strlen(html), "http://test.cz/dir/page.html", "text/html; charset=utf-8", -1);
}

static void expect_text(const char* html, const char* want)
{
    BwDoc* d = parse(html);
    char t[4096];
    if (!d) { printf("FAIL parse NULL\n"); fails++; return; }
    doc_text(d, t, sizeof(t));
    if (strcmp(t, want) != 0) { printf("FAIL text of %s\n  got  [%s]\n  want [%s]\n", html, t, want); fails++; }
    bw_doc_free(d);
}

static void test_html(void)
{
    expect_text("<p>Hello   <b>world</b> !</p><p>Second</p>", "|Hello world !|Second");
    expect_text("<title>T</title><script>var a='<p>x</p>';</script><style>p{}</style>Hi", "Hi");
    expect_text("a &amp; b &lt;c&gt; &copy; &#269;&#x161; &nbsp;x &bogus; AT&T", "a & b <c> \xC2\xA9 \xC4\x8D\xC5\xA1 \xC2\xA0x &bogus; AT&T");
    expect_text("<ul><li>one<li>two</ul>", "|\xE2\x80\xA2one|\xE2\x80\xA2two");
    expect_text("<ol start=3><li>a<li>b</ol>", "|3.a|4.b");
    expect_text("<div style='display:none'>hidden</div>shown", "shown");
    expect_text("<style>.x{display:none} #y {display:none} span.z{display:none}</style><p class='a x'>no</p><p id=y>no</p><span class=z>no</span><b class=z>yes</b>", "yes");
    expect_text("<style>@media (min-width: 1000px) { .d {display:none} } @media (max-width: 800px) { .m {display:none} }</style><p class=d>desk</p><p class=m>mob</p>", "|desk");
    expect_text("<pre>  a\n  b</pre>", "|  a\n  b");
    expect_text("<table><tr><td>a<td>b<tr><td>c</table>", "|ab|c");
    expect_text("x<br>y", "x|y");
    expect_text("<div><div>a</div></div><p>b", "|a|b");
    expect_text("<p>a<div>b</div>c", "|a|b|c");
    expect_text("<select name=s><option value=1>One<option selected>Two</select>", "[ctl9]");
    expect_text("<a href='javascript:void(0)'>js</a>", "js");
    expect_text("<head><title>x</title></head><body><noscript>ns</noscript>ok</body>", "|ns|ok");
    expect_text("caf\xC3\xA9", "caf\xC3\xA9");
    expect_text("<p>x</p >  <!-- c --> <p>y", "|x|y");
    expect_text("1 < 2 and 3 > 2", "1 < 2 and 3 > 2");
    expect_text("<span class=sr-only style='position:absolute;left:-9999px'>skip</span>main", "main");
    {
        char t[256];
        const char* h = "<meta charset=windows-1250><p>\x9A\xE8</p>";
        BwDoc* d = bw_doc_parse(h, (unsigned int)strlen(h), "http://x/", 0, -1);
        doc_text(d, t, sizeof(t));
        CHECK(d->charset == BW_CS_1250);
        CHECK(strcmp(t, "|\xC5\xA1\xC4\x8D") == 0);
        bw_doc_free(d);
    }
    {
        BwDoc* d = parse("<title> My  page </title><base href='http://other.cz/x/'><a href=y>l</a><img src=i.png alt=pic width=10>"
                         "<meta http-equiv=refresh content='0; url=/next'>");
        char t[256];
        CHECK(d->titleLen == 7);
        to8(d->title, d->titleLen, t, sizeof(t));
        CHECK(strcmp(t, "My page") == 0);
        CHECK(d->linkCount == 1 && strcmp(d->links[0].href, "http://other.cz/x/y") == 0);
        CHECK(d->imageCount == 1 && strcmp(d->images[0].src, "http://other.cz/x/i.png") == 0 && d->images[0].attrW == 10);
        CHECK(d->refreshUrl && strcmp(d->refreshUrl, "http://other.cz/next") == 0 && d->refreshDelay == 0);
        CHECK(d->targetCount == 1);
        bw_doc_free(d);
    }
    {
        BwDoc* d = parse("<body bgcolor=#000000 text=white link=yellow><a href=x>l</a>");
        CHECK(d->bgColor == 0 && d->textColor == 0xFFFFFF && d->linkColor == 0xFFFF00);
        bw_doc_free(d);
    }
}


static void test_css2(void)
{
    expect_text("<style>.menu a{display:none} div > b {display:none} #x .y i{display:none}</style>"
                "<div class=menu><p><a href=1>gone</a></p></div><a href=2>stays</a>"
                "<div><b>gone</b></div><p><b>kept</b></p>"
                "<div id=x><span class='y z'><i>gone</i></span></div><span class=y><i>kept</i></span>", "|stays|kept|kept");
    expect_text("<style>p.a{display:none} p{display:block} .b.c{display:none}</style><p class=a>x</p><p class='c b'>y</p><p class=b>z</p>", "|z");
    expect_text("<style>ul li + li{display:none} a:hover{display:none} [type]{display:none}</style><ul><li>a<li>b</ul>", "|\xE2\x80\xA2" "a|\xE2\x80\xA2" "b");
    {
        /* specificity: the id rule wins although it comes first */
        BwDoc* d = parse("<style>#t{color:#ff0000} p{color:#00ff00} .c{color:#0000ff}</style><p id=t class=c>x</p>");
        unsigned int i;
        int col = -1;
        for (i = 0; i < d->opCount; i++) if (d->ops[i].type == BW_OP_TEXT) col = d->ops[i].color;
        CHECK(col == 0xFF0000);
        bw_doc_free(d);
    }
    {
        /* a link colored by a descendant rule keeps it; a plain one gets the link color */
        BwDoc* d = parse("<style>nav a{color:#ffffff;text-decoration:none}</style><nav><a href=a>n</a></nav><a href=b>m</a>");
        unsigned int i;
        int c1 = -1, c2 = -1, f1 = -1;
        for (i = 0; i < d->opCount; i++) if (d->ops[i].type == BW_OP_TEXT) {
            if (d->pool[d->ops[i].off] == 'n') { c1 = d->ops[i].color; f1 = d->ops[i].flags; }
            if (d->pool[d->ops[i].off] == 'm') c2 = d->ops[i].color;
        }
        CHECK(c1 == 0xFFFFFF && !(f1 & BW_OPF_UNDERLINE));
        CHECK(c2 == d->linkColor);
        bw_doc_free(d);
    }
    {
        BwDoc* d = parse("<link rel=stylesheet href=/a.css><link rel='alternate stylesheet' href=b.css><link rel=stylesheet media=print href=p.css>"
                         "<link rel=icon href=i.ico><style>@import url(\"imp.css\"); @import 'pr.css' print; p{}</style>x");
        CHECK(d->styleCount == 2);
        if (d->styleCount == 2) {
            CHECK(strcmp(d->styleUrls[0], "http://test.cz/a.css") == 0);
            CHECK(strcmp(d->styleUrls[1], "http://test.cz/dir/imp.css") == 0);
        }
        bw_doc_free(d);
    }
    {
        const char* css[2];
        unsigned int cl[2];
        const char* h = "<p class=ad>ad</p><p>text</p>";
        BwDoc* d;
        char t[256];
        css[0] = ".ad{display:none}"; cl[0] = (unsigned int)strlen(css[0]);
        css[1] = 0; cl[1] = 0;
        d = bw_doc_parse_ex(h, (unsigned int)strlen(h), "http://x/", 0, -1, css, cl, 2);
        doc_text(d, t, sizeof(t));
        CHECK(strcmp(t, "|text") == 0);
        CHECK(d->externalCss == 2);
        bw_doc_free(d);
    }
}

static int font_of(const BwDoc* d, unsigned short c)
{
    unsigned int i;
    for (i = 0; i < d->opCount; i++) if (d->ops[i].type == BW_OP_TEXT && d->pool[d->ops[i].off] == c) return d->ops[i].font;
    return -1;
}

static void test_sizes(void)
{
    BwDoc* d = parse("<style>body{font-size:62.5%} p{font-size:1.4em} .s{font-size:11px} .r{font-size:2rem}</style>"
                     "<body>a<p>b<span class=s>c</span></p><h1>d</h1><small>e</small><p class=r>f</p><font size=5>g</font></body>");
    CHECK(BW_FONT_SIZE(font_of(d, 'a')) == 0);      /* 10 px */
    CHECK(BW_FONT_SIZE(font_of(d, 'b')) == 1);      /* 14 px */
    CHECK(BW_FONT_SIZE(font_of(d, 'c')) == 0);
    CHECK(BW_FONT_SIZE(font_of(d, 'd')) == 2 && (font_of(d, 'd') & BW_FONT_BOLD));   /* 2 x 10 px */
    CHECK(BW_FONT_SIZE(font_of(d, 'e')) == 0);
    CHECK(BW_FONT_SIZE(font_of(d, 'f')) == 3);      /* 32 px */
    CHECK(BW_FONT_SIZE(font_of(d, 'g')) == 2);
    bw_doc_free(d);
    d = parse("<h1>x</h1><h2>y</h2><h3>z</h3><p>w</p>");
    CHECK(BW_FONT_SIZE(font_of(d, 'x')) == 3 && BW_FONT_SIZE(font_of(d, 'y')) == 2 && BW_FONT_SIZE(font_of(d, 'z')) == 2 && BW_FONT_SIZE(font_of(d, 'w')) == 1);
    bw_doc_free(d);
}

static void test_forms(void)
{
    BwDoc* d = parse("<form action='/s' method=get><input name=q value='a b'><input type=hidden name=h value='\xC5\xA1'>"
                     "<input type=checkbox name=c value=1 checked><input type=checkbox name=d>"
                     "<input type=radio name=r value=x checked><input type=radio name=r value=y>"
                     "<select name=s><option value=1>One<option>Two</select><textarea name=t>line1\nline2</textarea>"
                     "<input type=submit name=go value=Go><button name=b value=bv>Press <b>me</b></button></form>");
    char* url; char* body; unsigned int bl; int post;
    unsigned int i;
    int sub = -1, btn = -1, radioY = -1, sel = -1;
    for (i = 0; i < d->controlCount; i++) {
        if (d->controls[i].type == BW_CTL_SUBMIT && d->controls[i].name && strcmp(d->controls[i].name, "go") == 0) sub = (int)i;
        if (d->controls[i].type == BW_CTL_SUBMIT && d->controls[i].name && strcmp(d->controls[i].name, "b") == 0) btn = (int)i;
        if (d->controls[i].type == BW_CTL_RADIO && d->controls[i].value[0] == 'y') radioY = (int)i;
        if (d->controls[i].type == BW_CTL_SELECT) sel = (int)i;
    }
    CHECK(sub >= 0 && btn >= 0 && radioY >= 0 && sel >= 0);
    {
        char t[64];
        to8(d->controls[btn].label, d->controls[btn].labelLen, t, sizeof(t));
        CHECK(strcmp(t, "Press me") == 0);
        CHECK(d->controls[sel].optionCount == 2);
    }
    bw_control_click(d, radioY);
    d->controls[sel].selected = 1;
    CHECK(bw_form_submit(d, 0, sub, &url, &body, &bl, &post));
    CHECK(!post && !body);
    if (url && strcmp(url, "http://test.cz/s?q=a+b&h=%C5%A1&c=1&r=y&s=Two&t=line1%0D%0Aline2&go=Go") != 0) { printf("FAIL submit %s\n", url); fails++; }
    free(url);
    bw_doc_free(d);

    d = bw_doc_parse("<form method=POST action=x.php><input name=n value='\x9A'></form>", (unsigned int)strlen("<form method=POST action=x.php><input name=n value='\x9A'></form>"), "http://test.cz/a/", 0, BW_CS_1250);
    CHECK(bw_form_submit(d, 0, -1, &url, &body, &bl, &post));
    CHECK(post && url && strcmp(url, "http://test.cz/a/x.php") == 0 && body && strcmp(body, "n=%9A") == 0);
    free(url); free(body);
    bw_doc_free(d);
}

static void test_layout(void)
{
    BwDoc* d = parse("<h1>Title</h1><p>one two three four five six seven eight nine ten eleven twelve</p>"
                     "<p style='text-align:center'>mid</p><a href=a>link</a> <img src=x.png width=40 height=30>"
                     "<input name=q size=10><a name=here></a>end");
    BwLayout* l = bw_layout(d, 200, &kM);
    unsigned int i;
    int sawMid = 0;
    CHECK(l && !l->failed && l->boxCount > 5);
    for (i = 0; i < l->boxCount; i++) {
        const BwBox* b = &l->boxes[i];
        CHECK(b->x >= 0 && b->x + b->w <= 200);
        if (b->kind == BW_BOX_TEXT && b->len == 3 && d->pool[b->off] == 'm') { sawMid = 1; CHECK(b->x > 60); }
    }
    CHECK(sawMid);
    CHECK(l->targets[0].y0 >= 0);
    CHECK(bw_layout_hit(l, l->targets[0].x0 + 1, l->targets[0].y0 + 1) == 0);
    CHECK(d->anchorCount == 1 && l->anchorY[0] > 0);
    d->images[0].natW = 400; d->images[0].natH = 300;
    bw_layout_free(l);
    l = bw_layout(d, 200, &kM);
    CHECK(l != 0);
    bw_layout_free(l);
    bw_doc_free(d);
}

/* the boxes of a text, by its first letter */
static const BwBox* box_of(const BwDoc* d, const BwLayout* l, unsigned short c)
{
    unsigned int i;
    for (i = 0; i < l->boxCount; i++) if (l->boxes[i].kind == BW_BOX_TEXT && d->pool[l->boxes[i].off] == c) return &l->boxes[i];
    return 0;
}

static void test_tables(void)
{
    BwDoc* d = parse("<table border=1><tr><th>Mena</th><th>Kurz</th></tr><tr><td>EUR</td><td align=right>24,35</td></tr>"
                     "<tr><td colspan=2>Zdroj: CNB</td></tr></table><p>after</p>");
    BwLayout* l = bw_layout(d, 500, &kM);
    const BwBox* mena = box_of(d, l, 'M');
    const BwBox* kurz = box_of(d, l, 'K');
    const BwBox* eur = box_of(d, l, 'E');
    const BwBox* val = box_of(d, l, '2');
    const BwBox* src = box_of(d, l, 'Z');
    const BwBox* aft = box_of(d, l, 'a');
    unsigned int i;
    CHECK(mena && kurz && eur && val && src && aft);
    if (mena && kurz && eur && val && src && aft) {
        CHECK(mena->y == kurz->y && kurz->x > mena->x + mena->w);    /* side by side */
        CHECK(eur->y > mena->y && val->y == eur->y);
        CHECK(val->x > mena->x + mena->w && val->x + val->w <= kurz->x + kurz->w + 8);   /* the same column */
        CHECK(src->y > eur->y && aft->y > src->y);
    }
    /* lines stay in order */
    for (i = 1; i < l->lineCount; i++) CHECK(l->lines[i].bottom >= l->lines[i - 1].bottom && l->lines[i].first >= l->lines[i - 1].first);
    {
        int outlines = 0;
        for (i = 0; i < l->fillCount; i++) if (l->fills[i].outline) outlines++;
        CHECK(outlines >= 5);
    }
    bw_layout_free(l);
    bw_doc_free(d);

    /* a sidebar after a long column: drawn from the row's line */
    d = parse("<table><tr><td>long<br>a<br>b<br>c<br>d<br>e</td><td>side</td></tr></table>");
    l = bw_layout(d, 300, &kM);
    {
        const BwBox* side = box_of(d, l, 's');
        const BwBox* lng = box_of(d, l, 'l');
        CHECK(side && lng && side->y == lng->y);
        CHECK(l->lineCount == 1);
        CHECK(bw_layout_first_box(l, 0) == 0);
        if (side) CHECK(bw_layout_hit(l, side->x + 1, side->y + 1) == -1);
    }
    bw_layout_free(l);
    bw_doc_free(d);

    /* rowspan: the next row's cell goes to the free column */
    d = parse("<table><tr><td rowspan=2>nav</td><td>top</td></tr><tr><td>bottom</td></tr></table>");
    l = bw_layout(d, 400, &kM);
    {
        const BwBox* top = box_of(d, l, 't');
        const BwBox* bot = box_of(d, l, 'b');
        CHECK(top && bot && bot->x == top->x && bot->y > top->y);
    }
    bw_layout_free(l);
    bw_doc_free(d);

    /* too wide for columns: the rows stack, the cells follow each other */
    d = parse("<table><tr><td>aaaaaaaaaaaaaaaaaaaaaaaaaaaa</td><td>bbbbbbbbbbbbbbbbbbbbbbbbbbb</td><td>ccccccccccccccccccccccc</td></tr></table>");
    l = bw_layout(d, 200, &kM);
    {
        const BwBox* a = box_of(d, l, 'a');
        const BwBox* b = box_of(d, l, 'b');
        CHECK(a && b && b->y > a->y);
    }
    bw_layout_free(l);
    bw_doc_free(d);

    /* nested tables, cells without rows, text outside cells */
    d = parse("<table><caption>Cap</caption><td>x<table><tr><td>in1<td>in2</table><td>y</table>");
    l = bw_layout(d, 400, &kM);
    {
        const BwBox* cap = box_of(d, l, 'C');
        const BwBox* x = box_of(d, l, 'x');
        const BwBox* y = box_of(d, l, 'y');
        CHECK(cap && x && y && cap->y < x->y && y->y == x->y && y->x > x->x);
    }
    bw_layout_free(l);
    bw_doc_free(d);
}

static void test_cookies(void)
{
    BwCookieJar j;
    char* h;
    char* saved;
    unsigned int now = 1793000000u;
    const char* c1 = "sid=abc; Path=/; Domain=.seznam.cz; Max-Age=3600";
    const char* c2 = "tmp=1";
    const char* c3 = "sid=; Max-Age=0; Domain=seznam.cz; Path=/";
    bw_cookies_init(&j);
    bw_cookies_set(&j, "www.seznam.cz", "/a/b", c1, (unsigned int)strlen(c1), now);
    bw_cookies_set(&j, "www.seznam.cz", "/a/b", c2, (unsigned int)strlen(c2), now);
    h = bw_cookies_header(&j, "email.seznam.cz", "/", 0, now);
    CHECK(h && strcmp(h, "sid=abc") == 0);
    free(h);
    h = bw_cookies_header(&j, "www.seznam.cz", "/a/c", 0, now);
    CHECK(h && strcmp(h, "sid=abc; tmp=1") == 0);
    free(h);
    h = bw_cookies_header(&j, "www.seznam.cz", "/", 0, now);
    CHECK(h && strcmp(h, "sid=abc") == 0);
    free(h);
    saved = bw_cookies_save(&j, now);
    CHECK(saved && strstr(saved, "sid") && !strstr(saved, "tmp"));
    bw_cookies_set(&j, "www.seznam.cz", "/", c3, (unsigned int)strlen(c3), now);
    h = bw_cookies_header(&j, "www.seznam.cz", "/", 0, now);
    CHECK(h == 0);
    bw_cookies_free(&j);
    bw_cookies_init(&j);
    bw_cookies_load(&j, saved, (unsigned int)strlen(saved), now);
    h = bw_cookies_header(&j, "x.seznam.cz", "/", 0, now);
    CHECK(h && strcmp(h, "sid=abc") == 0);
    free(h);
    free(saved);
    bw_cookies_free(&j);
    CHECK(bw_parse_http_date("Wed, 21 Oct 2015 07:28:00 GMT", 29) == 1445412480u);
}

static char* read_file(const char* name, unsigned int* len)
{
    FILE* f = fopen(name, "rb");
    long n;
    char* d;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (char*)malloc((size_t)n + 1);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) { /* short read */ }
    fclose(f);
    *len = (unsigned int)n;
    return d;
}

static const char* gCss = 0;
static unsigned int gCssLen = 0;

static void dump(const char* name)
{
    unsigned int len, i;
    char* data = read_file(name, &len);
    BwDoc* d;
    BwLayout* l;
    int lastY = -1000;
    if (!data) { printf("cannot read %s\n", name); return; }
    d = bw_doc_parse_ex(data, len, "http://example.cz/", 0, -1, &gCss, &gCssLen, gCss ? 1 : 0);
    l = bw_layout(d, 500, &kM);
    printf("ops %u, pool %u, links %u, images %u, controls %u, css rules %u, height %d, boxes %u\n",
           d->opCount, d->poolLen, d->linkCount, d->imageCount, d->controlCount, d->cssRules, l->height, l->boxCount);
    for (i = 0; i < l->boxCount; i++) {
        const BwBox* b = &l->boxes[i];
        char t[4096];
        if (b->y > lastY + 4) { printf("\n%4d ", b->y); lastY = b->y; }
        if (b->kind == BW_BOX_TEXT) { to8(d->pool + b->off, b->len, t, sizeof(t)); printf("%s%s%s ", b->target >= 0 ? "_" : "", t, b->target >= 0 ? "_" : ""); }
        else if (b->kind == BW_BOX_IMAGE) printf("[IMG %dx%d] ", b->w, b->h);
        else if (b->kind == BW_BOX_CONTROL) printf("[CTL %d] ", d->controls[b->ref].type);
        else printf("---- ");
    }
    printf("\n");
    bw_layout_free(l);
    bw_doc_free(d);
    free(data);
}

static void fuzz(const char* name)
{
    unsigned int len, it;
    char* data = read_file(name, &len);
    unsigned int seed = 12345;
    if (!data) return;
    for (it = 0; it < 3000; it++) {
        char* c = (char*)malloc(len + 1);
        unsigned int k, cut = len;
        BwDoc* d;
        memcpy(c, data, len);
        for (k = 0; k < 20; k++) {
            seed = seed * 1103515245u + 12345u;
            c[(seed >> 8) % (len ? len : 1)] = (char)(seed >> 3);
        }
        seed = seed * 1103515245u + 12345u;
        if (it & 1) cut = (seed >> 4) % (len ? len : 1);
        d = bw_doc_parse(c, cut, "http://f.cz/a/b", 0, (int)(it % 3) - 1);
        if (d) {
            BwLayout* l;
            for (k = 0; k < d->imageCount; k++) { d->images[k].natW = (int)(k * 37 % 900); d->images[k].natH = (int)(k * 53 % 700) + 1; }
            l = bw_layout(d, (int)(50 + it % 600), &kM);
            if (l) {
                bw_layout_hit(l, 100, 100);
                bw_layout_free(l);
            }
            for (k = 0; k < d->formCount; k++) {
                char* u; char* b; unsigned int bl; int post;
                if (bw_form_submit(d, (int)k, -1, &u, &b, &bl, &post)) { free(u); free(b); }
            }
            bw_doc_free(d);
        }
        free(c);
    }
    printf("fuzz: 3000 runs done\n");
    free(data);
}

int main(int argc, char** argv)
{
    if (argc == 2) { dump(argv[1]); return 0; }
    if (argc == 4 && strcmp(argv[1], "-css") == 0) {
        unsigned int n;
        gCss = read_file(argv[2], &n);
        gCssLen = n;
        dump(argv[3]);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "-fuzz") == 0) { fuzz(argv[2]); return 0; }
    test_urls();
    test_charsets();
    test_html();
    test_css2();
    test_sizes();
    test_forms();
    test_layout();
    test_tables();
    test_cookies();
    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails ? 1 : 0;
}
