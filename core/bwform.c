/* Browser 9300 - forms: values, clicks, application/x-www-form-urlencoded submissions. C89. */

#include "bwcore.h"

static const unsigned short kSubmit[] = { 'S', 'u', 'b', 'm', 'i', 't' };
static const unsigned short kReset[] = { 'R', 'e', 's', 'e', 't' };
static const unsigned short kFile[] = { '(', 'f', 'i', 'l', 'e', ')' };

int bw_control_set_value(BwControl* c, const unsigned short* s, unsigned int n)
{
    unsigned short* v = (unsigned short*)ig_malloc((n + 1) * 2);
    if (!v) return 0;
    if (n) ig_memcpy(v, s, n * 2);
    v[n] = 0;
    ig_free(c->value);
    c->value = v;
    c->valueLen = n;
    return 1;
}

static int same_name(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    return ig_streq(a, b);
}

void bw_control_click(BwDoc* doc, int ci)
{
    BwControl* c = &doc->controls[ci];
    if (c->type == BW_CTL_CHECKBOX) c->checked = !c->checked;
    else if (c->type == BW_CTL_RADIO) {
        unsigned int i;
        for (i = 0; i < doc->controlCount; i++) {
            BwControl* o = &doc->controls[i];
            if (o->type == BW_CTL_RADIO && o->form == c->form && same_name(o->name, c->name)) o->checked = 0;
        }
        c->checked = 1;
    } else if (c->type == BW_CTL_RESET) {
        unsigned int i;
        for (i = 0; i < doc->controlCount; i++) {
            BwControl* o = &doc->controls[i];
            if (o->form == c->form && (o->type == BW_CTL_CHECKBOX || o->type == BW_CTL_RADIO)) o->checked = o->initialChecked;
        }
    }
}

const unsigned short* bw_control_text(const BwControl* c, unsigned int* len)
{
    switch (c->type) {
    case BW_CTL_SELECT:
        if (c->optionCount && c->selected >= 0 && (unsigned int)c->selected < c->optionCount) {
            *len = c->options[c->selected].labelLen;
            return c->options[c->selected].label;
        }
        *len = 0;
        return 0;
    case BW_CTL_SUBMIT: case BW_CTL_BUTTON: case BW_CTL_IMAGE: case BW_CTL_RESET:
        if (c->labelLen) { *len = c->labelLen; return c->label; }
        if (c->valueLen) { *len = c->valueLen; return c->value; }
        if (c->type == BW_CTL_RESET) { *len = 5; return kReset; }
        *len = 6;
        return kSubmit;
    case BW_CTL_FILE:
        *len = 6;
        return kFile;
    default:
        *len = c->valueLen;
        return c->value;
    }
}

/* form-urlencodes UTF-16 text in the doc's charset */
static void put_enc16(IgBuf* b, const unsigned short* s, unsigned int n, int cs)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned int u = s[i];
        unsigned char bytes[4];
        int nb = 0, k;
        if (u == '\n' && (i == 0 || s[i - 1] != '\r')) {
            ig_buf_puts(b, "%0D%0A");
            continue;
        }
        if (cs == BW_CS_UTF8) {
            if (u >= 0xD800 && u < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
                unsigned int cp = 0x10000 + ((u - 0xD800) << 10) + (s[i + 1] - 0xDC00);
                i++;
                bytes[0] = (unsigned char)(0xF0 | (cp >> 18));
                bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
                bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
                bytes[3] = (unsigned char)(0x80 | (cp & 0x3F));
                nb = 4;
            } else if (u < 0x80) { bytes[0] = (unsigned char)u; nb = 1; }
            else if (u < 0x800) { bytes[0] = (unsigned char)(0xC0 | (u >> 6)); bytes[1] = (unsigned char)(0x80 | (u & 0x3F)); nb = 2; }
            else {
                bytes[0] = (unsigned char)(0xE0 | (u >> 12));
                bytes[1] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
                bytes[2] = (unsigned char)(0x80 | (u & 0x3F));
                nb = 3;
            }
        } else {
            int c = bw_charset_byte(cs, (unsigned short)u);
            if (c < 0) {
                /* what browsers send for a character the charset lacks */
                char num[12];
                ig_itoa((int)u, num);
                ig_buf_puts(b, "%26%23");
                ig_buf_puts(b, num);
                ig_buf_puts(b, "%3B");
                continue;
            }
            bytes[0] = (unsigned char)c;
            nb = 1;
        }
        for (k = 0; k < nb; k++) {
            unsigned char c = bytes[k];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '*' || c == '-' || c == '.' || c == '_') ig_buf_putc(b, (char)c);
            else if (c == ' ') ig_buf_putc(b, '+');
            else { ig_buf_putc(b, '%'); ig_buf_putc(b, hex[c >> 4]); ig_buf_putc(b, hex[c & 15]); }
        }
    }
}

static void put_name(IgBuf* b, const char* name, int cs, const char* suffix)
{
    unsigned int n = ig_strlen(name);
    unsigned int m = ig_utf8_to_utf16(name, n, 0, 0);
    unsigned short* u = (unsigned short*)ig_malloc((m + 1) * 2);
    if (!u) { b->failed = 1; return; }
    ig_utf8_to_utf16(name, n, u, m);
    if (b->len) ig_buf_putc(b, '&');
    put_enc16(b, u, m, cs);
    ig_free(u);
    if (suffix) ig_buf_puts(b, suffix);
    ig_buf_putc(b, '=');
}

int bw_form_submit(const BwDoc* doc, int form, int submitter, char** url, char** body,
                   unsigned int* bodyLen, int* post)
{
    IgBuf q;
    unsigned int i;
    const BwForm* f;
    int cs = doc->charset;
    *url = 0; *body = 0; *bodyLen = 0; *post = 0;
    if (form < 0 || (unsigned int)form >= doc->formCount) return 0;
    f = &doc->forms[form];
    ig_buf_init(&q);
    for (i = 0; i < doc->controlCount; i++) {
        const BwControl* c = &doc->controls[i];
        if (c->form != form || !c->name || c->disabled == 1) continue;
        switch (c->type) {
        case BW_CTL_TEXT: case BW_CTL_PASSWORD: case BW_CTL_HIDDEN: case BW_CTL_TEXTAREA:
            put_name(&q, c->name, cs, 0);
            put_enc16(&q, c->value, c->valueLen, cs);
            break;
        case BW_CTL_CHECKBOX: case BW_CTL_RADIO:
            if (!c->checked) break;
            put_name(&q, c->name, cs, 0);
            put_enc16(&q, c->value, c->valueLen, cs);
            break;
        case BW_CTL_SELECT:
            if (c->optionCount && c->selected >= 0 && (unsigned int)c->selected < c->optionCount) {
                put_name(&q, c->name, cs, 0);
                put_enc16(&q, c->options[c->selected].value, c->options[c->selected].valueLen, cs);
            }
            break;
        case BW_CTL_SUBMIT:
            if ((int)i != submitter) break;
            put_name(&q, c->name, cs, 0);
            put_enc16(&q, c->value, c->valueLen, cs);
            break;
        case BW_CTL_IMAGE:
            if ((int)i != submitter) break;
            put_name(&q, c->name, cs, ".x");
            ig_buf_putc(&q, '0');
            put_name(&q, c->name, cs, ".y");
            ig_buf_putc(&q, '0');
            break;
        default:
            break;
        }
    }
    if (q.failed) { ig_buf_free(&q); return 0; }
    *post = f->post;
    if (f->post) {
        *bodyLen = q.len;
        *body = ig_buf_detach(&q);
        *url = ig_strdup(f->action);
        if (!*body || !*url) { ig_free(*body); ig_free(*url); *body = 0; *url = 0; return 0; }
        return 1;
    } else {
        IgBuf u;
        unsigned int k, end = ig_strlen(f->action);
        for (k = 0; k < end; k++) if (f->action[k] == '?' || f->action[k] == '#') { end = k; break; }
        ig_buf_init(&u);
        ig_buf_append(&u, f->action, end);
        ig_buf_putc(&u, '?');
        if (q.len) ig_buf_append(&u, q.data, q.len);
        ig_buf_free(&q);
        *url = ig_buf_detach(&u);
        return *url != 0;
    }
}
