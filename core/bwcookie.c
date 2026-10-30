/* Browser 9300 - cookies: Set-Cookie parsing, matching, a text file for the persistent ones. C89. */

#include "bwcore.h"

#define MAX_COOKIES 300

void bw_cookies_init(BwCookieJar* j) { ig_memset(j, 0, sizeof(BwCookieJar)); }

static void free_cookie(BwCookie* c)
{
    ig_free(c->domain); ig_free(c->path); ig_free(c->name); ig_free(c->value);
}

void bw_cookies_free(BwCookieJar* j)
{
    unsigned int i;
    for (i = 0; i < j->count; i++) free_cookie(&j->items[i]);
    ig_free(j->items);
    ig_memset(j, 0, sizeof(BwCookieJar));
}

static int is_ws(int c) { return c == ' ' || c == '\t'; }
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* "Wed, 21 Oct 2026 07:28:00 GMT" (also "21-Oct-26") */
unsigned int bw_parse_http_date(const char* s, unsigned int n)
{
    static const char months[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    unsigned int i = 0;
    int day = -1, mon = -1, year = -1, hh = 0, mm = 0, ss = 0, gotTime = 0;
    while (i < n) {
        unsigned int st;
        while (i < n && !((s[i] >= '0' && s[i] <= '9') || (lower((unsigned char)s[i]) >= 'a' && lower((unsigned char)s[i]) <= 'z'))) i++;
        st = i;
        if (i < n && s[i] >= '0' && s[i] <= '9') {
            int v = 0;
            while (i < n && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
            if (i < n && s[i] == ':' && !gotTime) {
                hh = v; i++;
                v = 0; while (i < n && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
                mm = v;
                if (i < n && s[i] == ':') { i++; v = 0; while (i < n && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; } ss = v; }
                gotTime = 1;
            } else if (day < 0 && i - st <= 2) day = v;
            else if (year < 0) year = v;
        } else {
            while (i < n && lower((unsigned char)s[i]) >= 'a' && lower((unsigned char)s[i]) <= 'z') i++;
            if (i - st >= 3 && mon < 0) {
                int k;
                for (k = 0; k < 12; k++)
                    if (lower((unsigned char)s[st]) == months[k * 3] && lower((unsigned char)s[st + 1]) == months[k * 3 + 1] &&
                        lower((unsigned char)s[st + 2]) == months[k * 3 + 2]) { mon = k; break; }
            }
        }
    }
    if (day < 1 || mon < 0 || year < 0) return 0;
    if (year < 70) year += 2000; else if (year < 100) year += 1900;
    if (year < 1970) return 0;
    if (year > 2105) year = 2105;
    {
        /* days from 1970-01-01 (civil calendar) */
        int y = year, m = mon + 1;
        long days;
        int era, yoe, doy, doe;
        y -= m <= 2;
        era = y / 400;
        yoe = y - era * 400;
        doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        days = (long)era * 146097L + doe - 719468L;
        return (unsigned int)(days * 86400L + hh * 3600L + mm * 60L + ss);
    }
}

/* does host fall under the cookie domain */
static int domain_match(const char* host, const char* domain, int hostOnly)
{
    unsigned int hl = ig_strlen(host), dl = ig_strlen(domain);
    if (hl == dl) return ig_strieq_n(host, domain, hl);
    if (hostOnly || hl < dl + 1) return 0;
    return host[hl - dl - 1] == '.' && ig_strieq_n(host + hl - dl, domain, dl);
}

static int path_match(const char* reqPath, const char* cpath)
{
    unsigned int cl = ig_strlen(cpath);
    if (ig_memcmp(reqPath, cpath, cl) != 0) {
        /* "/a" matches the request "/a" without the trailing slash */
        return 0;
    }
    return cl == 0 || cpath[cl - 1] == '/' || reqPath[cl] == 0 || reqPath[cl] == '/' || reqPath[cl] == '?';
}

static void remove_at(BwCookieJar* j, unsigned int i)
{
    free_cookie(&j->items[i]);
    for (; i + 1 < j->count; i++) j->items[i] = j->items[i + 1];
    j->count--;
}

static void store(BwCookieJar* j, BwCookie* c, unsigned int now)
{
    unsigned int i;
    for (i = 0; i < j->count; i++) {
        BwCookie* o = &j->items[i];
        if (ig_streq(o->name, c->name) && ig_strieq_n(o->domain, c->domain, ig_strlen(c->domain) + 1) && ig_streq(o->path, c->path)) {
            remove_at(j, i);
            break;
        }
    }
    if (c->expires && c->expires <= now) { free_cookie(c); return; }     /* a deletion */
    if (j->count >= MAX_COOKIES) remove_at(j, 0);
    if (j->count >= j->cap) {
        unsigned int nc = j->cap ? j->cap * 2 : 32;
        BwCookie* na = (BwCookie*)ig_malloc(nc * sizeof(BwCookie));
        if (!na) { free_cookie(c); return; }
        if (j->items) { ig_memcpy(na, j->items, j->count * sizeof(BwCookie)); ig_free(j->items); }
        j->items = na;
        j->cap = nc;
    }
    j->items[j->count++] = *c;
}

void bw_cookies_set(BwCookieJar* j, const char* host, const char* path, const char* h, unsigned int n, unsigned int now)
{
    BwCookie c;
    unsigned int i = 0, ns, ne, vs, ve;
    ig_memset(&c, 0, sizeof(c));
    while (i < n && is_ws((unsigned char)h[i])) i++;
    ns = i;
    while (i < n && h[i] != '=' && h[i] != ';') i++;
    ne = i;
    while (ne > ns && is_ws((unsigned char)h[ne - 1])) ne--;
    if (i >= n || h[i] != '=' || ne == ns) return;
    i++;
    while (i < n && is_ws((unsigned char)h[i])) i++;
    vs = i;
    while (i < n && h[i] != ';') i++;
    ve = i;
    while (ve > vs && is_ws((unsigned char)h[ve - 1])) ve--;
    c.name = ig_strndup(h + ns, ne - ns);
    c.value = ig_strndup(h + vs, ve - vs);
    c.hostOnly = 1;
    {
        int maxAgeSet = 0;
        while (i < n) {
            unsigned int as, ae, avs, ave;
            i++;
            while (i < n && is_ws((unsigned char)h[i])) i++;
            as = i;
            while (i < n && h[i] != '=' && h[i] != ';') i++;
            ae = i;
            while (ae > as && is_ws((unsigned char)h[ae - 1])) ae--;
            avs = ave = i;
            if (i < n && h[i] == '=') {
                i++;
                while (i < n && is_ws((unsigned char)h[i])) i++;
                avs = i;
                while (i < n && h[i] != ';') i++;
                ave = i;
                while (ave > avs && is_ws((unsigned char)h[ave - 1])) ave--;
            }
            if (ae - as == 6 && ig_strieq_n(h + as, "domain", 6) && ave > avs) {
                const char* dv = h + avs;
                unsigned int dl = ave - avs;
                while (dl && dv[0] == '.') { dv++; dl--; }
                if (dl) {
                    char* dom = ig_strndup(dv, dl);
                    if (dom && domain_match(host, dom, 0)) { ig_free(c.domain); c.domain = dom; c.hostOnly = 0; }
                    else ig_free(dom);
                }
            } else if (ae - as == 4 && ig_strieq_n(h + as, "path", 4) && ave > avs && h[avs] == '/') {
                ig_free(c.path);
                c.path = ig_strndup(h + avs, ave - avs);
            } else if (ae - as == 7 && ig_strieq_n(h + as, "expires", 7) && !maxAgeSet) {
                unsigned int t = bw_parse_http_date(h + avs, ave - avs);
                c.expires = t ? t : 0;
                if (t && t <= now) c.expires = 1;
            } else if (ae - as == 7 && ig_strieq_n(h + as, "max-age", 7)) {
                long v = 0;
                int neg = 0;
                unsigned int k = avs;
                if (k < ave && h[k] == '-') { neg = 1; k++; }
                while (k < ave && h[k] >= '0' && h[k] <= '9') { v = v * 10 + (h[k] - '0'); if (v > 400000000L) v = 400000000L; k++; }
                c.expires = (neg || v == 0) ? 1 : now + (unsigned int)v;
                maxAgeSet = 1;
            } else if (ae - as == 6 && ig_strieq_n(h + as, "secure", 6)) c.secure = 1;
        }
    }
    if (!c.domain) c.domain = ig_strdup(host);
    if (!c.path) {
        /* the directory of the request path */
        unsigned int pl = ig_strlen(path), k = pl, q;
        for (q = 0; q < pl; q++) if (path[q] == '?') { k = q; break; }
        while (k > 0 && path[k - 1] != '/') k--;
        if (k > 1) k--;
        c.path = k <= 1 ? ig_strdup("/") : ig_strndup(path, k);
    }
    if (!c.name || !c.value || !c.domain || !c.path) { free_cookie(&c); return; }
    store(j, &c, now);
}

char* bw_cookies_header(const BwCookieJar* j, const char* host, const char* path, int secure, unsigned int now)
{
    IgBuf b;
    unsigned int i;
    ig_buf_init(&b);
    for (i = 0; i < j->count; i++) {
        const BwCookie* c = &j->items[i];
        if (c->expires && c->expires <= now) continue;
        if (c->secure && !secure) continue;
        if (!domain_match(host, c->domain, c->hostOnly)) continue;
        if (!path_match(path, c->path)) continue;
        if (b.len) ig_buf_puts(&b, "; ");
        ig_buf_puts(&b, c->name);
        ig_buf_putc(&b, '=');
        ig_buf_puts(&b, c->value);
    }
    if (!b.len) { ig_buf_free(&b); return 0; }
    return ig_buf_detach(&b);
}

/* one line per cookie: expires TAB hostOnly TAB secure TAB domain TAB path TAB name TAB value */
char* bw_cookies_save(const BwCookieJar* j, unsigned int now)
{
    IgBuf b;
    unsigned int i;
    ig_buf_init(&b);
    for (i = 0; i < j->count; i++) {
        const BwCookie* c = &j->items[i];
        char num[12];
        if (!c->expires || c->expires <= now) continue;
        ig_itoa((int)(c->expires > 2000000000u ? 2000000000u : c->expires), num);
        ig_buf_puts(&b, num);
        ig_buf_putc(&b, '\t'); ig_buf_putc(&b, c->hostOnly ? '1' : '0');
        ig_buf_putc(&b, '\t'); ig_buf_putc(&b, c->secure ? '1' : '0');
        ig_buf_putc(&b, '\t'); ig_buf_puts(&b, c->domain);
        ig_buf_putc(&b, '\t'); ig_buf_puts(&b, c->path);
        ig_buf_putc(&b, '\t'); ig_buf_puts(&b, c->name);
        ig_buf_putc(&b, '\t'); ig_buf_puts(&b, c->value);
        ig_buf_putc(&b, '\n');
    }
    return ig_buf_detach(&b);
}

void bw_cookies_load(BwCookieJar* j, const char* t, unsigned int n, unsigned int now)
{
    unsigned int i = 0;
    while (i < n) {
        unsigned int fs[7], fe[7], k = 0, ls = i;
        while (i < n && t[i] != '\n') i++;
        {
            unsigned int p = ls;
            while (k < 7 && p <= i) {
                fs[k] = p;
                while (p < i && t[p] != '\t' && t[p] != '\r') p++;
                fe[k] = p;
                k++;
                if (p >= i || t[p] == '\r') break;
                p++;
            }
        }
        if (i < n) i++;
        if (k == 7) {
            BwCookie c;
            unsigned int e = 0, q;
            for (q = fs[0]; q < fe[0]; q++) e = e * 10 + (unsigned int)(t[q] - '0');
            if (e <= now) continue;
            ig_memset(&c, 0, sizeof(c));
            c.expires = e;
            c.hostOnly = t[fs[1]] == '1';
            c.secure = t[fs[2]] == '1';
            c.domain = ig_strndup(t + fs[3], fe[3] - fs[3]);
            c.path = ig_strndup(t + fs[4], fe[4] - fs[4]);
            c.name = ig_strndup(t + fs[5], fe[5] - fs[5]);
            c.value = ig_strndup(t + fs[6], fe[6] - fs[6]);
            if (!c.domain || !c.path || !c.name || !c.value) { free_cookie(&c); continue; }
            store(j, &c, now);
        }
    }
}
