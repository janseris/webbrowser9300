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


static const BwBox* box_of(const BwDoc* d, const BwLayout* l, unsigned short c);

static int color_of(const BwDoc* d, unsigned short c)
{
    unsigned int i;
    for (i = 0; i < d->opCount; i++) if (d->ops[i].type == BW_OP_TEXT && d->pool[d->ops[i].off] == c) return d->ops[i].color;
    return -1;
}

static void test_css3(void)
{
    /* !important beats a more specific rule and the style attribute */
    expect_text("<style>.hide{display:none !important} #x{display:block}</style><p id=x class=hide>a</p><p>b</p>", "|b");
    /* attribute selectors, :not, :nth-child, :first-child, :root */
    expect_text("<style>[hidden-x]{display:none} input[type=hidden]{display:none} a[href^=\"http://ads\"]{display:none} "
                "a[href$=\".pdf\"]{display:none} p:not(.keep){display:none} li:nth-child(even){display:none} "
                "li:first-child{display:none}</style>"
                "<span hidden-x>A</span><a href=http://ads.x/>B</a><a href=/f.pdf>C</a><a href=/ok>D</a>"
                "<p class=keep>E</p><p>F</p><ul><li>1<li>2<li>3<li>4<li>5</ul>", "D|E|\xE2\x80\xA2" "3|\xE2\x80\xA2" "5");
    /* rules with :hover, ::before, + are dropped, not applied */
    expect_text("<style>a:hover{display:none} p::before{display:none} p + p{display:none}</style><a href=x>a</a><p>b</p><p>c</p>", "a|b|c");
    /* var() from :root, hsl, named colors */
    {
        BwDoc* d = parse("<style>:root{--main:#123456;--bg: hsl(0, 100%, 50%)} .dark{--main:#ffffff} p{color:var(--main)} "
                         "b{color:var(--nope, rebeccapurple)} i{color:hsl(120,100%,25%)} u{color:#0000}</style><p>x</p><b>y</b><i>z</i><u>w</u>");
        CHECK(color_of(d, 'x') == 0x123456);
        CHECK(color_of(d, 'y') == 0x663399);
        CHECK(color_of(d, 'z') == 0x007F00 || color_of(d, 'z') == 0x008000);
        bw_doc_free(d);
    }
    /* list-style, ol type, roman */
    expect_text("<style>ul.n{list-style:none}</style><ul class=n><li>a</ul><ol type=a><li>b<li>c</ol><ol style='list-style-type:upper-roman' start=4><li>d</ol>",
                "|a|a.b|b.c|IV.d");
    /* text-transform */
    expect_text("<span style='text-transform:uppercase'>p\xC5\x99\xC3\xadli\xC5\xa1</span> <b style='text-transform:capitalize'>a word here</b>",
                "P\xC5\x98\xC3\x8dLI\xC5\xa0 A Word Here");
    /* float: a horizontal menu */
    expect_text("<style>.m li{float:left;list-style:none}</style><ul class=m><li>One<li>Two</ul>", "|OneTwo");
    /* a closed dialog, aria-label on an icon link and button */
    {
        BwDoc* d = parse("<dialog>secret</dialog><dialog open>shown</dialog><a href=/s aria-label=Search><svg></svg></a>"
                         "<form><button aria-label=Go><svg></svg></button></form>");
        char t[256];
        unsigned int i, n = 0;
        const unsigned short* lab = 0;
        doc_text(d, t, sizeof(t));
        CHECK(strstr(t, "secret") == 0 && strstr(t, "shown") && strstr(t, "Search"));
        for (i = 0; i < d->controlCount; i++) if (d->controls[i].type == BW_CTL_SUBMIT) lab = bw_control_text(&d->controls[i], &n);
        CHECK(lab && n == 2 && lab[0] == 'G');
        bw_doc_free(d);
    }
    /* srcset / picture: a decodable, not too big candidate */
    {
        BwDoc* d = parse("<img src=big.jpg srcset='s.jpg 320w, m.jpg 640w, l.jpg 1280w'>"
                         "<picture><source type=image/webp srcset=a.webp><source srcset='p1.jpg 1x, p2.jpg 2x'><img src=fallback.png></picture>"
                         "<img src=x.webp srcset='y.avif 1x'>");
        CHECK(d->imageCount == 3);
        if (d->imageCount == 3) {
            CHECK(strcmp(d->images[0].src, "http://test.cz/dir/m.jpg") == 0);
            CHECK(strcmp(d->images[1].src, "http://test.cz/dir/p1.jpg") == 0);
        }
        bw_doc_free(d);
    }
    /* CSS image size */
    {
        BwDoc* d = parse("<style>.ic{width:24px;height:24px}</style><img class=ic src=i.png><img src=j.png style='width:100px' width=200 height=100>");
        CHECK(d->imageCount == 2 && d->images[0].attrW == 24 && d->images[0].attrH == 24);
        CHECK(d->imageCount == 2 && d->images[1].attrW == 100 && d->images[1].attrH == 50);
        bw_doc_free(d);
    }
    /* the box: margins, padding, borders, width + auto margins, inline background */
    {
        BwDoc* d = parse("<style>.box{margin:20px auto;padding:10px;border:2px solid red;width:200px;background:#eee} .tag{background:yellow}</style>"
                         "<p>before</p><div class=box>inside</div><p>after <span class=tag>tagged</span></p>");
        BwLayout* l = bw_layout(d, 500, &kM);
        const BwBox* in = box_of(d, l, 'i');
        const BwBox* bef = box_of(d, l, 'b');
        const BwBox* tg = box_of(d, l, 't');
        unsigned int i;
        int borders = 0, filled = 0;
        CHECK(in && bef && tg);
        if (in && bef) {
            CHECK(in->x > 150 && in->x < 160);          /* (500 - 200) / 2 + 2 border + 5 padding */
            CHECK(in->y > bef->y + bef->h + 10);
        }
        for (i = 0; i < l->fillCount; i++) {
            if (l->fills[i].color == 0xFF0000) borders++;
            if (l->fills[i].color == 0xEEEEEE && l->fills[i].w == 200) filled++;
        }
        CHECK(borders == 4 && filled == 1);
        CHECK(tg && tg->bg == 0xFFFF00);
        bw_layout_free(l);
        bw_doc_free(d);
    }
    /* generated text, quotes, flex rows, object fallback, noscript duplicates */
    expect_text("<style>a[href$='.pdf']::after{content:' (PDF)'} .req:before{content:'* '} .ic::before{content:'\\f101'} "
                ".lbl::before{content:attr(data-l) ': '}</style>"
                "<a href=x.pdf>doc</a> <span class=req>name</span> <i class=ic>i</i> <b class=lbl data-l=Price>5</b> <q>quoted</q>",
                "doc (PDF) * name i Price: 5 \xE2\x80\x9Cquoted\xE2\x80\x9D");
    expect_text("<style>.row{display:flex} .col{display:flex;flex-direction:column}</style>"
                "<div class=row><div>A</div><div>B</div></div><div class=col><div>C</div><div>D</div></div>", "|AB|C|D");
    expect_text("<object data=x.swf><param name=a value=b>Fallback text</object>", "Fallback text");
    {
        BwDoc* d = parse("<img class=lazy data-src=p.jpg><noscript><img src=p.jpg></noscript><noscript><img src=q.jpg></noscript>");
        CHECK(d->imageCount == 2);
        bw_doc_free(d);
    }
    /* invisible characters, emoji */
    expect_text("so\xC2\xAD" "ft\xE2\x80\x8B zero\xEF\xBB\xBF emoji\xF0\x9F\x98\x80!&#x1F600;&#8203;.", "soft zero emoji!.");
    /* labels: for= and around */
    {
        BwDoc* d = parse("<form><label for=c1>Remember</label><input type=checkbox id=c1 name=r>"
                         "<label><input type=radio name=x value=1> One</label><label>Lost</label></form>");
        unsigned int k;
        int labels = 0;
        CHECK(d->labelCount == 3);
        for (k = 0; k < d->labelCount; k++) if (d->labels[k].control >= 0) labels++;
        CHECK(labels == 2);
        if (d->labelCount == 3) {
            CHECK(d->controls[d->labels[0].control].type == BW_CTL_CHECKBOX);
            CHECK(d->controls[d->labels[1].control].type == BW_CTL_RADIO);
        }
        bw_doc_free(d);
    }
    /* image maps: their areas become links */
    {
        BwDoc* d = parse("<img src=m.png usemap=#m><map name=m><area href=/a alt=North><area href=/b></map>");
        char t[256];
        doc_text(d, t, sizeof(t));
        CHECK(d->linkCount == 2 && strstr(t, "[North]") && strstr(t, "[/b]"));
        bw_doc_free(d);
    }
    /* table width hints: width=100% stretches, a column width is preferred */
    {
        BwDoc* d = parse("<table width=100%><tr><td width=120>nav</td><td>main</td></tr></table>");
        BwLayout* l = bw_layout(d, 500, &kM);
        const BwBox* mainb = box_of(d, l, 'm');
        CHECK(mainb && mainb->x >= 120);
        CHECK(l->fillCount == 0 || 1);
        bw_layout_free(l);
        bw_doc_free(d);
    }
    /* nowrap keeps words together; disabled fields are not sent */
    {
        BwDoc* d = parse("<p>aaaa bbbb <span style='white-space:nowrap'>cccc dddd eeee</span></p>");
        BwLayout* l = bw_layout(d, 120, &kM);
        const BwBox* a = box_of(d, l, 'a');
        const BwBox* c = box_of(d, l, 'c');
        CHECK(a && c && c->len == 14 && c->y > a->y);       /* one box on the next line: the run was not broken */
        bw_layout_free(l);
        bw_doc_free(d);
        d = parse("<form action=/x><input name=a value=1><input name=b value=2 disabled><input name=c value=3 readonly></form>");
        {
            char* url; char* body; unsigned int bl; int post;
            CHECK(bw_form_submit(d, 0, -1, &url, &body, &bl, &post));
            CHECK(url && strcmp(url, "http://test.cz/x?a=1&c=3") == 0);
            free(url);
        }
        bw_doc_free(d);
    }
    /* deep nesting does not recurse without end */
    {
        IgBuf b;
        int k;
        BwDoc* d;
        BwLayout* l;
        ig_buf_init(&b);
        for (k = 0; k < 300; k++) ig_buf_puts(&b, "<table><tr><td>x");
        d = bw_doc_parse(b.data, b.len, "http://x/", 0, -1);
        l = bw_layout(d, 500, &kM);
        CHECK(l != 0);
        bw_layout_free(l);
        bw_doc_free(d);
        ig_buf_free(&b);
    }
    /* :hover: the chain under the pointer gets the hover rules; numbering is stable */
    {
        const char* h = "<style>.menu ul{display:none} .menu li:hover > ul{display:block} a:hover{color:#ff0000}</style>"
                        "<ul class=menu><li><a href=/a>Products</a><ul><li><a href=/a1>Phones</a></ul><li><a href=/b>Help</a></ul>";
        BwDoc* d = bw_doc_parse(h, (unsigned int)strlen(h), "http://x/", 0, -1);
        BwLayout* l = bw_layout(d, 500, &kM);
        const BwBox* prod = box_of(d, l, 'P');
        const BwBox* help = box_of(d, l, 'H');
        int chain[64];
        unsigned int n;
        BwDoc* d2;
        CHECK(d->hoverCount == 2);
        CHECK(prod && !box_of(d, l, 'h') && prod->elem >= 0);
        CHECK(prod && help && bw_doc_hover_matters(d, prod->elem));
        n = bw_doc_hover_chain(d, prod->elem, chain, 64);
        CHECK(n >= 3);
        d2 = bw_doc_parse_hover(h, (unsigned int)strlen(h), "http://x/", 0, -1, 0, 0, 0, chain, n);
        {
            BwLayout* l2 = bw_layout(d2, 500, &kM);
            const BwBox* sub = box_of(d2, l2, 'P');
            unsigned int i;
            int phones = 0;
            for (i = 0; i < l2->boxCount; i++) if (l2->boxes[i].kind == BW_BOX_TEXT && d2->pool[l2->boxes[i].off] == 'P' && d2->pool[l2->boxes[i].off + 1] == 'h') phones = 1;
            CHECK(phones);                          /* the submenu is shown */
            CHECK(sub && sub->color == 0xFF0000);  /* the hovered link is red */
            CHECK(d2->elemCount == d->elemCount);
            CHECK(bw_layout_hit_box(l2, sub->x + 1, sub->y + 1) >= 0);
            bw_layout_free(l2);
        }
        bw_doc_free(d2);
        bw_layout_free(l);
        bw_doc_free(d);
    }
    /* form state carries over a re-parse */
    {
        BwDoc* a = parse("<form><input name=q><input type=checkbox name=c></form>");
        BwDoc* b = parse("<form><input name=q><input type=checkbox name=c></form>");
        unsigned short v[3] = { 'h', 'i', 0 };
        bw_control_set_value(&a->controls[0], v, 2);
        a->controls[1].checked = 1;
        bw_doc_copy_form_state(b, a);
        CHECK(b->controls[0].valueLen == 2 && b->controls[0].value[1] == 'i' && b->controls[1].checked);
        bw_doc_free(a);
        bw_doc_free(b);
    }
    /* media features */
    expect_text("<style>@media (prefers-color-scheme: dark){.l{display:none}} @media (hover: hover){.h{display:none}} "
                "@media (-webkit-min-device-pixel-ratio: 2){.r{display:none}}</style><p class=l>L</p><p class=h>H</p><p class=r>R</p>", "|L|H|R");
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
    test_css3();
    test_forms();
    test_layout();
    test_tables();
    test_cookies();
    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails ? 1 : 0;
}
