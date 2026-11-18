/* NegxusOS - biblioteca grafica 32 bpp (canvas, texto antialiased, formas) */
#include "kernel.h"

nx_canvas g_screen;
static u32 *fb;            /* framebuffer linear (endereco fisico = virtual) */
static int fb_pitch;       /* em pixels */

void gfx_init(const bootinfo_t *bi) {
    fb = (u32 *)(u64)bi->fb;
    fb_pitch = bi->pitch / 4;
    u32 *back = kzalloc((size_t)bi->w * bi->h * 4);
    g_screen = cv_make(back, bi->w, bi->h, bi->w);
    kprintf("[gfx] %dx%d fb=%lx\n", bi->w, bi->h, (u64)fb);
}

void gfx_present(int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 > g_screen.w) x1 = g_screen.w; if (y1 > g_screen.h) y1 = g_screen.h;
    if (x >= x1 || y >= y1) return;
    for (int j = y; j < y1; j++) {
        const u32 *s = g_screen.px + j * g_screen.pitch + x;
        u32 *d = fb + j * fb_pitch + x;
        u64 n = x1 - x;
        asm volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    }
}

nx_canvas cv_make(u32 *px, int w, int h, int pitch) {
    nx_canvas c = { px, w, h, pitch, 0, 0, w, h };
    return c;
}
void cv_clip(nx_canvas *c, int x, int y, int w, int h) {
    c->cx0 = MAX(x, 0); c->cy0 = MAX(y, 0);
    c->cx1 = MIN(x + w, c->w); c->cy1 = MIN(y + h, c->h);
}

int isqrt32(u32 v) {
    u32 r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (int)r;
}
u32 rgb_mix(u32 a, u32 b, int t) { return blend(a, b, t); }

/* ---------------- retangulos ---------------- */
static inline int clip_rect(const nx_canvas *c, int *x, int *y, int *w, int *h) {
    int x1 = *x + *w, y1 = *y + *h;
    if (*x < c->cx0) *x = c->cx0;
    if (*y < c->cy0) *y = c->cy0;
    if (x1 > c->cx1) x1 = c->cx1;
    if (y1 > c->cy1) y1 = c->cy1;
    *w = x1 - *x; *h = y1 - *y;
    return *w > 0 && *h > 0;
}
void cv_fill(nx_canvas *c, int x, int y, int w, int h, u32 col) {
    if (!clip_rect(c, &x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) {
        u32 *p = c->px + (y + j) * c->pitch + x; u64 n = w;
        asm volatile("rep stosl" : "+D"(p), "+c"(n) : "a"(col) : "memory");
    }
}
void cv_fill_a(nx_canvas *c, int x, int y, int w, int h, u32 col, int a) {
    if (a >= 255) { cv_fill(c, x, y, w, h, col); return; }
    if (a <= 0 || !clip_rect(c, &x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) {
        u32 *p = c->px + (y + j) * c->pitch + x;
        for (int i = 0; i < w; i++) p[i] = blend(p[i], col, a);
    }
}
void cv_rect(nx_canvas *c, int x, int y, int w, int h, u32 col) {
    cv_fill(c, x, y, w, 1, col); cv_fill(c, x, y + h - 1, w, 1, col);
    cv_fill(c, x, y + 1, 1, h - 2, col); cv_fill(c, x + w - 1, y + 1, 1, h - 2, col);
}
void cv_gradient_v(nx_canvas *c, int x, int y, int w, int h, u32 top, u32 bot) {
    for (int j = 0; j < h; j++) cv_fill(c, x, y + j, w, 1, blend(top, bot, h > 1 ? j * 255 / (h - 1) : 0));
}

/* ---------------- cantos arredondados com antialiasing ---------------- */
#define MAXR 24
static u8 cov_tab[MAXR + 1][MAXR * MAXR];
static u8 cov_ready[MAXR + 1];
static int corner_cov(int r, int ix, int iy) {
    int dx = ix * 16 + 8, dy = iy * 16 + 8;
    int d = isqrt32((u32)(dx * dx + dy * dy));
    int cv = r * 16 - d + 8;
    if (cv <= 0) return 0;
    if (cv >= 16) return 255;
    return cv * 255 / 16;
}
static const u8 *cov_for(int r) {
    if (!cov_ready[r]) {
        for (int j = 0; j < r; j++) for (int i = 0; i < r; i++)
            cov_tab[r][j * r + i] = corner_cov(r, r - 1 - i, r - 1 - j);
        cov_ready[r] = 1;
    }
    return cov_tab[r];
}
static inline void plot_a(nx_canvas *c, int x, int y, u32 col, int a) {
    if (x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1 || a <= 0) return;
    u32 *p = c->px + y * c->pitch + x;
    *p = blend(*p, col, a);
}
void cv_rrect_a(nx_canvas *c, int x, int y, int w, int h, int r, u32 col, int a) {
    if (r > MAXR) r = MAXR;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) { cv_fill_a(c, x, y, w, h, col, a); return; }
    cv_fill_a(c, x, y + r, w, h - 2 * r, col, a);
    cv_fill_a(c, x + r, y, w - 2 * r, r, col, a);
    cv_fill_a(c, x + r, y + h - r, w - 2 * r, r, col, a);
    const u8 *t = cov_for(r);
    for (int j = 0; j < r; j++) for (int i = 0; i < r; i++) {
        int cv = t[j * r + i] * a / 255;
        if (!cv) continue;
        plot_a(c, x + i, y + j, col, cv);
        plot_a(c, x + w - 1 - i, y + j, col, cv);
        plot_a(c, x + i, y + h - 1 - j, col, cv);
        plot_a(c, x + w - 1 - i, y + h - 1 - j, col, cv);
    }
}
void cv_rrect(nx_canvas *c, int x, int y, int w, int h, int r, u32 col) { cv_rrect_a(c, x, y, w, h, r, col, 255); }
void cv_rrect_line(nx_canvas *c, int x, int y, int w, int h, int r, u32 col) {
    if (r > MAXR) r = MAXR;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 1) { cv_rect(c, x, y, w, h, col); return; }
    cv_fill(c, x + r, y, w - 2 * r, 1, col); cv_fill(c, x + r, y + h - 1, w - 2 * r, 1, col);
    cv_fill(c, x, y + r, 1, h - 2 * r, col); cv_fill(c, x + w - 1, y + r, 1, h - 2 * r, col);
    const u8 *to = cov_for(r), *ti = cov_for(r - 1);
    for (int j = 0; j < r; j++) for (int i = 0; i < r; i++) {
        int inner = (i >= 1 && j >= 1) ? ti[(j - 1) * (r - 1) + (i - 1)] : 0;
        int cv = to[j * r + i] - inner;
        if (cv <= 0) continue;
        plot_a(c, x + i, y + j, col, cv);
        plot_a(c, x + w - 1 - i, y + j, col, cv);
        plot_a(c, x + i, y + h - 1 - j, col, cv);
        plot_a(c, x + w - 1 - i, y + h - 1 - j, col, cv);
    }
}

void cv_circle(nx_canvas *c, int cx, int cy, int r, u32 col) {
    for (int py = cy - r; py <= cy + r; py++) for (int px = cx - r; px <= cx + r; px++) {
        int dx = (px - cx) * 16, dy = (py - cy) * 16;
        int d = isqrt32((u32)(dx * dx + dy * dy));
        int cv = r * 16 - d + 8;
        if (cv <= 0) continue;
        plot_a(c, px, py, col, cv >= 16 ? 255 : cv * 255 / 16);
    }
}

void cv_line(nx_canvas *c, int x0, int y0, int x1, int y1, u32 col) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= c->cx0 && x0 < c->cx1 && y0 >= c->cy0 && y0 < c->cy1) c->px[y0 * c->pitch + x0] = col;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ---------------- texto ---------------- */
int text_height(int fi) { return font_table[fi].height; }
int text_width(const char *s, int fi) {
    const font_t *f = &font_table[fi];
    int w = 0;
    for (; *s; s++) {
        u8 ch = (u8)*s;
        if (ch == '\t') { w += f->g[0].adv * 4; continue; }
        if (ch < 0x20) continue;
        w += f->g[ch - 0x20].adv;
    }
    return w;
}
void cv_text(nx_canvas *c, int x, int y, const char *s, int fi, u32 col) {
    const font_t *f = &font_table[fi];
    int base = y + f->ascent + 1;
    for (; *s; s++) {
        u8 ch = (u8)*s;
        if (ch == '\t') { x += f->g[0].adv * 4; continue; }
        if (ch < 0x20) continue;
        const glyph_t *g = &f->g[ch - 0x20];
        if (g->w) {
            int gx = x + g->bx, gy = base + g->by;
            const u8 *al = f->alpha + g->off;
            for (int j = 0; j < g->h; j++) {
                int py = gy + j;
                if (py < c->cy0 || py >= c->cy1) continue;
                u32 *row = c->px + py * c->pitch;
                for (int i = 0; i < g->w; i++) {
                    int px = gx + i, a = al[j * g->w + i];
                    if (a && px >= c->cx0 && px < c->cx1) row[px] = blend(row[px], col, a);
                }
            }
        }
        x += g->adv;
        if (x >= c->cx1) break;
    }
}

/* ---------------- copia e icones ---------------- */
void cv_blit(nx_canvas *d, int x, int y, const nx_canvas *s, int sx, int sy, int w, int h) {
    if (sx < 0) { x -= sx; w += sx; sx = 0; }
    if (sy < 0) { y -= sy; h += sy; sy = 0; }
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    int dx0 = MAX(x, d->cx0), dy0 = MAX(y, d->cy0);
    int dx1 = MIN(x + w, d->cx1), dy1 = MIN(y + h, d->cy1);
    if (dx0 >= dx1 || dy0 >= dy1) return;
    for (int j = dy0; j < dy1; j++) {
        const u32 *sp = s->px + (sy + j - y) * s->pitch + sx + (dx0 - x);
        u32 *dp = d->px + j * d->pitch + dx0;
        u64 n = dx1 - dx0;
        asm volatile("rep movsl" : "+D"(dp), "+S"(sp), "+c"(n) : : "memory");
    }
}

void cv_icon(nx_canvas *c, const u32 *ic, int x, int y, int size) {
    const int S = NXE_ICON_PX;
    if (!ic || size <= 0) return;
    for (int j = 0; j < size; j++) {
        int py = y + j;
        if (py < c->cy0 || py >= c->cy1) continue;
        int sy0 = j * S / size, sy1 = MAX((j + 1) * S / size, sy0 + 1);
        for (int i = 0; i < size; i++) {
            int px = x + i;
            if (px < c->cx0 || px >= c->cx1) continue;
            int sx0 = i * S / size, sx1 = MAX((i + 1) * S / size, sx0 + 1);
            u32 sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
            for (int yy = sy0; yy < sy1; yy++) for (int xx = sx0; xx < sx1; xx++) {
                u32 p = ic[yy * S + xx], a = p >> 24;
                sa += a; sr += ((p >> 16) & 255) * a; sg += ((p >> 8) & 255) * a; sb += (p & 255) * a; n++;
            }
            if (!sa) continue;
            u32 col = RGB(sr / sa, sg / sa, sb / sa);
            u32 *dp = c->px + py * c->pitch + px;
            *dp = blend(*dp, col, sa / n);
        }
    }
}

const u8 *gfx_corner_cov(int r) { return r > MAXR ? cov_for(MAXR) : cov_for(r); }
