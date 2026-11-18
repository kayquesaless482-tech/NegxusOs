/* libc minima compartilhada (kernel + aplicativos) */
#include "../sdk/nxe.h"

void *memcpy(void *d, const void *s, size_t n) {
    void *r = d;
    asm volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s;
    if (dd == ss || n == 0) return d;
    if (dd < ss || dd >= ss + n) { memcpy(d, s, n); return d; }
    dd += n; ss += n;
    while (n--) *--dd = *--ss;
    return d;
}
void *memset(void *d, int c, size_t n) {
    void *r = d;
    asm volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (uint8_t)*a - (uint8_t)*b : 0;
}
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int strcasecmp(const char *a, const char *b) {
    while (*a && lc((uint8_t)*a) == lc((uint8_t)*b)) { a++; b++; }
    return lc((uint8_t)*a) - lc((uint8_t)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && lc((uint8_t)*a) == lc((uint8_t)*b)) { a++; b++; n--; }
    return n ? lc((uint8_t)*a) - lc((uint8_t)*b) : 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c) {
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}
char *strrchr(const char *s, int c) {
    const char *r = 0;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) break; }
    return (char *)r;
}
char *strstr(const char *h, const char *n) {
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (*h == *n && !strncmp(h, n, l)) return (char *)h;
    return 0;
}
int atoi(const char *s) {
    int neg = 0, v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

/* ---- snprintf: %d %i %u %x %X %c %s %p %%, flags '-' '0', largura, 'l' 'll' ---- */
typedef struct { char *buf; size_t n, len; } out_t;
static void putc_o(out_t *o, char c) { if (o->len + 1 < o->n) o->buf[o->len] = c; o->len++; }

int nx_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    out_t o = { buf, n, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { putc_o(&o, *fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0, lng = 0, prec = -1;
        while (*fmt == '-' || *fmt == '0') { if (*fmt == '-') left = 1; else zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') { fmt++; prec = 0; while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0'); }
        while (*fmt == 'l') { lng++; fmt++; }
        char tmp[24]; const char *s = tmp; int sl = 0; int neg = 0;
        char c = *fmt;
        if (c == '%') { putc_o(&o, '%'); continue; }
        if (c == 'c') { tmp[0] = (char)va_arg(ap, int); sl = 1; }
        else if (c == 's') {
            s = va_arg(ap, const char *); if (!s) s = "(null)";
            sl = (int)strlen(s); if (prec >= 0 && sl > prec) sl = prec;
        } else if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'p') {
            uint64_t v; int base = 10;
            if (c == 'd' || c == 'i') {
                int64_t sv = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
                if (sv < 0) { neg = 1; v = (uint64_t)(-sv); } else v = (uint64_t)sv;
            } else {
                v = (lng || c == 'p') ? va_arg(ap, uint64_t) : va_arg(ap, unsigned int);
                if (c != 'u') base = 16;
            }
            const char *dg = (c == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char r[24]; int k = 0;
            if (v == 0) r[k++] = '0';
            while (v) { r[k++] = dg[v % base]; v /= base; }
            if (neg) tmp[sl++] = '-';
            while (k) tmp[sl++] = r[--k];
        } else { putc_o(&o, c); continue; }
        int pad = width > sl ? width - sl : 0;
        if (!left && !zero) while (pad--) putc_o(&o, ' ');
        int i = 0;
        if (zero && !left && neg) { putc_o(&o, '-'); i = 1; }
        if (!left && zero) while (pad--) putc_o(&o, '0');
        for (; i < sl; i++) putc_o(&o, s[i]);
        if (left) while (pad--) putc_o(&o, ' ');
    }
    if (n) buf[o.len < n ? o.len : n - 1] = 0;
    return (int)o.len;
}
int nx_snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = nx_vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

char *strncat(char *d, const char *s, size_t n) {
    char *r = d; d += strlen(d);
    while (n-- && *s) *d++ = *s++;
    *d = 0;
    return r;
}
