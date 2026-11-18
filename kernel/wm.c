/* NegxusOS - gerenciador de janelas: composicao, foco, arrastar/redimensionar, eventos */
#include "wm.h"

window_t *g_wins[MAXWIN];
int g_nwin;
window_t *g_active;
int g_sw, g_sh;
int g_cur_x, g_cur_y;
int g_wm_ready;
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1;      /* retangulo sujo (vazio se x0>=x1) */
static window_t *capture;                               /* janela com o mouse "preso" */
static int cap_btn;
static int serial_ctr;

/* arrasto / redimensionamento */
static struct { window_t *w; int mode; int ox, oy; int sx, sy, sw, sh; } drag;   /* mode 1=mover 2..4=redimensionar */
static u32 last_click_t; static int last_click_x, last_click_y, last_click_b;


#define COL_TITLE_I   RGB(128, 128, 128)
#define COL_BORDER_A  RGB(192, 192, 192)
#define COL_BORDER_I  RGB(192, 192, 192)
#define COL_TEXT      RGB(255, 255, 255)
#define COL_TEXT_I    RGB(192, 192, 192)

/* ================= dano ================= */
void wm_damage(int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 > g_sw) x1 = g_sw; if (y1 > g_sh) y1 = g_sh;
    if (x >= x1 || y >= y1) return;
    if (dirty_x0 >= dirty_x1) { dirty_x0 = x; dirty_y0 = y; dirty_x1 = x1; dirty_y1 = y1; return; }
    if (x < dirty_x0) dirty_x0 = x;
    if (y < dirty_y0) dirty_y0 = y;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
}
void wm_damage_win(window_t *w) { wm_damage(w->x - 14, w->y - 10, w->w + 28, w->h + 30); }
int wm_needs_redraw(void) { return dirty_x0 < dirty_x1; }

/* ================= moldura ================= */
static int btn_x(window_t *w, int which) {          /* 0=min 1=max 2=close ; -1 se ausente */
    int fixed = w->flags & NW_FIXED;
    int close_x = w->w - 1 - 46;
    if (which == 2) return close_x;
    if (which == 1) return fixed ? -1 : close_x - 46;
    return fixed ? close_x - 46 : close_x - 92;
}
static void draw_chrome(window_t *w) {
    nx_canvas c = cv_make(w->surf, w->w, w->h, w->w);
    int act = (g_active == w);
    /* estilo Windows 10: barra de titulo clara, borda fina, botoes planos */
    cv_fill(&c, 0, 0, w->w, w->h, RGB(255, 255, 255));
    cv_fill(&c, 0, 0, w->w, TITLE_H, act ? RGB(255, 255, 255) : RGB(240, 240, 240));
    cv_rect(&c, 0, 0, w->w, w->h, act ? RGB(0, 120, 215) : RGB(170, 170, 170));
    int tx = 12;
    if (w->has_icon) { cv_icon(&c, w->icon, 10, 9, 18); tx = 36; }
    u32 txt = act ? RGB(0, 0, 0) : RGB(110, 110, 110);
    char buf[64]; strncpy(buf, w->title, 63); buf[63] = 0;
    int maxw = btn_x(w, 0) - tx - 8;
    while (strlen(buf) > 3 && text_width(buf, NF_UI) > maxw) { buf[strlen(buf) - 1] = 0; }
    cv_text(&c, tx, (TITLE_H - text_height(NF_UI)) / 2, buf, NF_UI, txt);
    for (int b = 0; b < 3; b++) {
        int bx = btn_x(w, b);
        if (bx < 0) continue;
        int hov = (w->hover_btn == b);
        u32 fg = act ? RGB(0, 0, 0) : RGB(110, 110, 110);
        if (hov) {
            if (b == 2) { cv_fill(&c, bx, 0, 46, TITLE_H, RGB(232, 17, 35)); fg = RGB(255, 255, 255); }
            else cv_fill(&c, bx, 0, 46, TITLE_H, RGB(229, 229, 229));
        }
        int cx = bx + 23, cy = TITLE_H / 2;
        if (b == 0) cv_fill(&c, cx - 5, cy, 10, 1, fg);
        else if (b == 1) {
            if (w->maximized) { cv_rect(&c, cx - 3, cy - 5, 8, 8, fg); cv_rect(&c, cx - 5, cy - 3, 8, 8, fg); }
            else cv_rect(&c, cx - 5, cy - 5, 10, 10, fg);
        } else {
            cv_line(&c, cx - 5, cy - 5, cx + 5, cy + 5, fg);
            cv_line(&c, cx - 5, cy + 5, cx + 5, cy - 5, fg);
            cv_line(&c, cx - 4, cy - 5, cx + 5, cy + 4, fg);
            cv_line(&c, cx - 5, cy + 4, cx + 4, cy - 5, fg);
        }
    }
}
static void damage_title(window_t *w) { wm_damage(w->x, w->y, w->w, TITLE_H); }

static void setup_client(window_t *w) {
    int cw = w->w - 2, ch = w->h - TITLE_H - 1;
    w->pub.cv = cv_make(w->surf + TITLE_H * w->w + 1, cw, ch, w->w);
}

/* ================= criacao / destruicao ================= */
extern int nxe_current_app(void);
extern const u32 *nxe_instance_icon(int id);

nx_win *wm_win_create(const char *title, int cw, int ch, int flags) {
    if (g_nwin >= MAXWIN) return 0;
    int old = g_alloc_tag; g_alloc_tag = 0;
    window_t *w = kzalloc(sizeof(window_t));
    if (!w) { g_alloc_tag = old; return 0; }
    int ow = cw + 2, oh = ch + TITLE_H + 1;
    int maxw = g_sw - 8, maxh = g_sh - TB_H - 8;
    if (ow > maxw) ow = maxw;
    if (oh > maxh) oh = maxh;
    w->w = ow; w->h = oh;
    w->surf = kzalloc((size_t)ow * oh * 4);
    if (!w->surf) { kfree(w); g_alloc_tag = old; return 0; }
    int n = g_nwin % 8;
    w->x = 70 + n * 32; w->y = 40 + n * 32;
    if (w->x + ow > g_sw - 4) w->x = MAX(4, g_sw - 4 - ow);
    if (w->y + oh > g_sh - TB_H - 4) w->y = MAX(4, g_sh - TB_H - 4 - oh);
    strncpy(w->title, title, 63);
    w->flags = flags; w->app = old; w->hover_btn = -1;
    w->min_cw = 120; w->min_ch = 60;
    w->serial = ++serial_ctr;
    w->need_paint = 1;
    const u32 *ic = nxe_instance_icon(old);
    if (ic) { memcpy(w->icon, ic, sizeof w->icon); w->has_icon = 1; }
    setup_client(w);
    cv_fill(&w->pub.cv, 0, 0, w->pub.cv.w, w->pub.cv.h, 0xFFFFFF);
    g_wins[g_nwin++] = w;
    g_alloc_tag = old;
    wm_focus(w);
    draw_chrome(w);
    wm_damage_win(w);
    return &w->pub;
}

static void win_destroy(window_t *w) {
    int idx = -1;
    for (int i = 0; i < g_nwin; i++) if (g_wins[i] == w) idx = i;
    if (idx < 0) return;
    for (int i = idx; i < g_nwin - 1; i++) g_wins[i] = g_wins[i + 1];
    g_nwin--;
    if (capture == w) capture = 0;
    if (drag.w == w) drag.w = 0;
    wm_damage_win(w);
    int app = w->app;
    kfree(w->surf); kfree(w);
    if (g_active == w) {
        g_active = 0;
        for (int i = g_nwin - 1; i >= 0; i--) if (!g_wins[i]->minimized) { wm_focus(g_wins[i]); break; }
    }
    nxe_win_closed(app);
}

void wm_close(window_t *w) {
    if (w->dead) return;
    nxe_set_current(w->app);
    int ok = 1;
    if (w->pub.on_close) ok = w->pub.on_close(&w->pub);
    nxe_set_current(0);
    if (ok) w->dead = 1;
}

window_t *wm_find_app_window(int app) {
    for (int i = 0; i < g_nwin; i++) if (g_wins[i]->app == app && !g_wins[i]->dead) return g_wins[i];
    return 0;
}

void wm_close_app_windows(int app_id) {
    for (int i = 0; i < g_nwin; i++) if (g_wins[i]->app == app_id) g_wins[i]->dead = 1;
}

/* ================= foco / estado ================= */
void wm_focus(window_t *w) {
    window_t *prev = g_active;
    int idx = -1;
    for (int i = 0; i < g_nwin; i++) if (g_wins[i] == w) idx = i;
    if (idx < 0) return;
    if (w->minimized) { w->minimized = 0; wm_damage_win(w); }
    if (idx != g_nwin - 1) {
        for (int i = idx; i < g_nwin - 1; i++) g_wins[i] = g_wins[i + 1];
        g_wins[g_nwin - 1] = w;
        wm_damage_win(w);
    }
    g_active = w;
    if (prev && prev != w) { draw_chrome(prev); damage_title(prev); }
    draw_chrome(w); damage_title(w);
    wm_damage(0, g_sh - TB_H, g_sw, TB_H);
}
void wm_minimize(window_t *w) {
    w->minimized = 1;
    wm_damage_win(w);
    if (g_active == w) {
        g_active = 0;
        for (int i = g_nwin - 1; i >= 0; i--) if (!g_wins[i]->minimized) { wm_focus(g_wins[i]); break; }
    }
    wm_damage(0, g_sh - TB_H, g_sw, TB_H);
}
void wm_restore(window_t *w) { wm_focus(w); }

static void set_geometry(window_t *w, int x, int y, int nw, int nh) {
    wm_damage_win(w);
    int resized = (nw != w->w || nh != w->h);
    if (resized) {
        int old = g_alloc_tag; g_alloc_tag = 0;
        u32 *ns = kzalloc((size_t)nw * nh * 4);
        g_alloc_tag = old;
        if (!ns) return;
        kfree(w->surf);
        w->surf = ns; w->w = nw; w->h = nh;
        setup_client(w);
        cv_fill(&w->pub.cv, 0, 0, w->pub.cv.w, w->pub.cv.h, 0xFFFFFF);
    }
    w->x = x; w->y = y;
    draw_chrome(w);
    if (resized) {
        nxe_set_current(w->app);
        if (w->pub.on_resize) w->pub.on_resize(&w->pub);
        if (w->pub.on_paint) w->pub.on_paint(&w->pub);
        nxe_set_current(0);
    }
    wm_damage_win(w);
}

static void toggle_max(window_t *w) {
    if (w->flags & NW_FIXED) return;
    if (!w->maximized) {
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
        w->maximized = 1;
        set_geometry(w, 0, 0, g_sw, g_sh - TB_H);
    } else {
        w->maximized = 0;
        set_geometry(w, w->rx, w->ry, w->rw, w->rh);
    }
    draw_chrome(w);
}

void nx_win_set_size(nx_win *p, int cw, int ch) {
    window_t *w = (window_t *)p;
    int ow = MIN(cw + 2, g_sw), oh = MIN(ch + TITLE_H + 1, g_sh - TB_H);
    w->maximized = 0;
    int x = MIN(w->x, MAX(0, g_sw - ow)), y = MIN(w->y, MAX(0, g_sh - TB_H - oh));
    set_geometry(w, x, y, ow, oh);
}

/* ================= composicao ================= */
static void draw_shadow(nx_canvas *c, window_t *w) {
    for (int k = 1; k <= 12; k++) {
        int a = 30 * (13 - k) * (13 - k) / 144;
        if (a <= 0) continue;
        int x = w->x - k, y = w->y - k + 5, ww = w->w + 2 * k, hh = w->h + 2 * k;
        cv_fill_a(c, x, y, ww, 1, 0, a);
        cv_fill_a(c, x, y + hh - 1, ww, 1, 0, a);
        cv_fill_a(c, x, y + 1, 1, hh - 2, 0, a);
        cv_fill_a(c, x + ww - 1, y + 1, 1, hh - 2, 0, a);
    }
}
static void draw_window(nx_canvas *c, window_t *w) {
    nx_canvas s = cv_make(w->surf, w->w, w->h, w->w);
    if (w->maximized) { cv_blit(c, w->x, w->y, &s, 0, 0, w->w, w->h); return; }
    draw_shadow(c, w);
    int r = WIN_R;
    cv_blit(c, w->x, w->y + r, &s, 0, r, w->w, w->h - 2 * r);
    cv_blit(c, w->x + r, w->y, &s, r, 0, w->w - 2 * r, r);
    cv_blit(c, w->x + r, w->y + w->h - r, &s, r, w->h - r, w->w - 2 * r, r);
    const u8 *t = gfx_corner_cov(r);
    for (int j = 0; j < r; j++) for (int i = 0; i < r; i++) {
        int a = t[j * r + i];
        int px[4] = { i, w->w - 1 - i, i, w->w - 1 - i };
        int py[4] = { j, j, w->h - 1 - j, w->h - 1 - j };
        for (int k = 0; k < 4; k++) {
            int dx = w->x + px[k], dy = w->y + py[k];
            if (dx < c->cx0 || dy < c->cy0 || dx >= c->cx1 || dy >= c->cy1) continue;
            u32 *d = &c->px[dy * c->pitch + dx];
            *d = blend(*d, w->surf[py[k] * w->w + px[k]], a);
        }
    }
}

void wm_redraw(void) {
    if (!wm_needs_redraw()) return;
    int x = dirty_x0, y = dirty_y0, w = dirty_x1 - dirty_x0, h = dirty_y1 - dirty_y0;
    dirty_x0 = dirty_y0 = dirty_x1 = dirty_y1 = 0;
    nx_canvas *c = &g_screen;
    cv_clip(c, x, y, w, h);
    desk_draw_background(c);
    for (int i = 0; i < g_nwin; i++) {
        window_t *wi = g_wins[i];
        if (wi->minimized) continue;
        if (wi->x - 14 > x + w || wi->x + wi->w + 14 < x || wi->y - 10 > y + h || wi->y + wi->h + 24 < y) continue;
        draw_window(c, wi);
    }
    desk_draw_overlay(c);
    desk_draw_cursor(c);
    cv_clip(c, 0, 0, c->w, c->h);
    gfx_present(x, y, w, h);
}

/* ================= entrada ================= */
static window_t *window_at(int x, int y) {
    for (int i = g_nwin - 1; i >= 0; i--) {
        window_t *w = g_wins[i];
        if (w->minimized || w->dead) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return w;
    }
    return 0;
}

static void send_mouse(window_t *w, int type, int x, int y, int btn, int arg) {
    if (!w->pub.on_mouse || w->dead) return;
    nxe_set_current(w->app);
    w->pub.on_mouse(&w->pub, type, x - (w->x + 1), y - (w->y + TITLE_H), btn, arg);
    nxe_set_current(0);
}

static void set_hover(window_t *w, int b) {
    if (w->hover_btn == b) return;
    w->hover_btn = b; draw_chrome(w); damage_title(w);
}

static int edge_hit(window_t *w, int mx, int my) {       /* 0 nenhum, 2=dir, 3=baixo, 4=canto */
    if ((w->flags & NW_FIXED) || w->maximized) return 0;
    int rx = mx - w->x, ry = my - w->y;
    int r = rx >= w->w - 6, b = ry >= w->h - 6;
    if (r && b) return 4;
    if (r && ry > TITLE_H) return 2;
    if (b) return 3;
    return 0;
}

static void on_move(int mx, int my, int btn) {
    desk_set_cursor_shape(NC_ARROW);
    if (drag.w) {
        window_t *w = drag.w;
        if (drag.mode == 1) {
            int nx = mx - drag.ox, ny = my - drag.oy;
            nx = CLAMP(nx, 60 - w->w, g_sw - 60);
            ny = CLAMP(ny, 0, g_sh - TB_H - 24);
            if (nx != w->x || ny != w->y) { wm_damage_win(w); w->x = nx; w->y = ny; wm_damage_win(w); }
        } else {
            int nw = drag.sw, nh = drag.sh;
            if (drag.mode == 2 || drag.mode == 4) nw = drag.sw + (mx - drag.sx);
            if (drag.mode == 3 || drag.mode == 4) nh = drag.sh + (my - drag.sy);
            nw = CLAMP(nw, w->pub.cv.w ? w->min_cw + 2 : 120, g_sw - w->x);
            nh = CLAMP(nh, w->min_ch + TITLE_H + 1, g_sh - TB_H - w->y);
            if (nw != w->w || nh != w->h) set_geometry(w, w->x, w->y, nw, nh);
        }
        desk_set_cursor_shape(drag.mode == 1 ? NC_ARROW : (drag.mode == 2 ? NC_SIZEH : drag.mode == 3 ? NC_SIZEV : NC_SIZEBR));
        return;
    }
    window_t *tgt = capture;
    if (!tgt && !desk_menu_open()) tgt = window_at(mx, my);
    if (!capture && desk_menu_open()) {
        /* menus abertos: so o desktop trata */
    }
    for (int i = 0; i < g_nwin; i++) if (g_wins[i] != tgt) set_hover(g_wins[i], -1);
    if (tgt && !capture && my >= g_sh - TB_H) tgt = 0;                     /* barra de tarefas cobre */
    if (!tgt) { desk_mouse(NMOUSE_MOVE, mx, my, btn, 0); return; }
    int rx = mx - tgt->x, ry = my - tgt->y;
    if (!capture && ry < TITLE_H) {
        int hb = -1;
        for (int b = 0; b < 3; b++) { int bx = btn_x(tgt, b); if (bx >= 0 && rx >= bx && rx < bx + 46) hb = b; }
        set_hover(tgt, hb);
        return;
    }
    set_hover(tgt, -1);
    if (!capture) {
        int e = edge_hit(tgt, mx, my);
        if (e) { desk_set_cursor_shape(e == 2 ? NC_SIZEH : e == 3 ? NC_SIZEV : NC_SIZEBR); return; }
    }
    tgt->pub.cursor = NC_ARROW;
    send_mouse(tgt, NMOUSE_MOVE, mx, my, btn, 0);
    desk_set_cursor_shape(tgt->pub.cursor);
}

static void on_button(int mx, int my, int btn, int changed) {
    int down = (btn & changed) != 0;
    if (!down) {                                       /* soltou */
        if (drag.w) { drag.w = 0; }
        if (capture) {
            window_t *c = capture;
            if (!(btn)) capture = 0;
            send_mouse(c, NMOUSE_UP, mx, my, btn, changed);
        } else desk_mouse(NMOUSE_UP, mx, my, btn, changed);
        return;
    }
    u32 now = g_ticks;
    int dbl = (now - last_click_t < 450 && changed == last_click_b && ABS_I(mx - last_click_x) < 5 && ABS_I(my - last_click_y) < 5);
    last_click_t = dbl ? 0 : now; last_click_x = mx; last_click_y = my; last_click_b = changed;

    if (desk_menu_open() || my >= g_sh - TB_H) {
        if (desk_mouse(NMOUSE_DOWN, mx, my, btn, changed)) return;
    }
    window_t *w = window_at(mx, my);
    if (!w) { desk_mouse(NMOUSE_DOWN, mx, my, btn, changed); if (dbl) desk_mouse(NMOUSE_DBL, mx, my, btn, changed); return; }
    if (g_active != w) wm_focus(w);
    int rx = mx - w->x, ry = my - w->y;
    if (ry < TITLE_H) {
        if (changed != NB_LEFT) return;
        for (int b = 0; b < 3; b++) {
            int bx = btn_x(w, b);
            if (bx >= 0 && rx >= bx && rx < bx + 46) {
                if (b == 0) wm_minimize(w);
                else if (b == 1) toggle_max(w);
                else wm_close(w);
                return;
            }
        }
        if (dbl) { toggle_max(w); return; }
        if (!w->maximized) { drag.w = w; drag.mode = 1; drag.ox = mx - w->x; drag.oy = my - w->y; }
        return;
    }
    int e = edge_hit(w, mx, my);
    if (e && changed == NB_LEFT) {
        drag.w = w; drag.mode = e; drag.sx = mx; drag.sy = my; drag.sw = w->w; drag.sh = w->h;
        return;
    }
    capture = w; cap_btn = changed;
    send_mouse(w, NMOUSE_DOWN, mx, my, btn, changed);
    if (dbl) send_mouse(w, NMOUSE_DBL, mx, my, btn, changed);
}

static int prev_btn;
void wm_handle_event(const event_t *e) {
    if (e->type == EV_MOUSE) {
        int ox = g_cur_x, oy = g_cur_y;
        g_cur_x = CLAMP(g_cur_x + e->a, 0, g_sw - 1);
        g_cur_y = CLAMP(g_cur_y + e->b, 0, g_sh - 1);
        wm_damage(ox - 2, oy - 2, 26, 30);
        wm_damage(g_cur_x - 2, g_cur_y - 2, 26, 30);
        int btn = e->c, changed = btn ^ prev_btn;
        if (e->a || e->b) on_move(g_cur_x, g_cur_y, btn);
        if (changed) {
            for (int b = 1; b <= 4; b <<= 1) if (changed & b) {
                prev_btn = (btn & b) ? (prev_btn | b) : (prev_btn & ~b);
                on_button(g_cur_x, g_cur_y, prev_btn, b);
            }
        }
        if (e->d) {
            window_t *w = capture ? capture : (desk_menu_open() ? 0 : window_at(g_cur_x, g_cur_y));
            if (w && (capture || g_cur_y < g_sh - TB_H)) send_mouse(w, NMOUSE_WHEEL, g_cur_x, g_cur_y, btn, e->d);
            else desk_mouse(NMOUSE_WHEEL, g_cur_x, g_cur_y, btn, e->d);
        }
        return;
    }
    if (e->type == EV_KEY) {
        int key = e->a, mods = e->b;
        if (desk_key(key, mods)) return;
        if ((mods & NM_ALT) && key == NK_F4 && g_active) { wm_close(g_active); return; }
        if ((mods & NM_ALT) && key == NK_TAB) {
            for (int i = 0; i < g_nwin; i++) if (!g_wins[i]->minimized && g_wins[i] != g_active) { wm_focus(g_wins[i]); break; }
            return;
        }
        if (g_active && g_active->pub.on_key && !g_active->dead) {
            nxe_set_current(g_active->app);
            g_active->pub.on_key(&g_active->pub, key, mods);
            nxe_set_current(0);
        }
    }
}

void wm_tick(void) {
    /* primeira pintura de janelas recem criadas (o app define on_paint depois de criar) */
    for (int i = 0; i < g_nwin; i++) {
        window_t *w = g_wins[i];
        if (w->need_paint && !w->dead && w->pub.on_paint) {
            w->need_paint = 0;
            nxe_set_current(w->app);
            w->pub.on_paint(&w->pub);
            nxe_set_current(0);
            wm_damage_win(w);
        }
    }
    /* temporizadores dos aplicativos */
    for (int i = 0; i < g_nwin; i++) {
        window_t *w = g_wins[i];
        if (w->dead || !w->timer_ms || !w->pub.on_timer) continue;
        if ((s32)(g_ticks - w->next_timer) >= 0) {
            w->next_timer = g_ticks + w->timer_ms;
            nxe_set_current(w->app);
            w->pub.on_timer(&w->pub);
            nxe_set_current(0);
        }
    }
    /* janelas marcadas para fechar */
    for (int i = 0; i < g_nwin; i++) if (g_wins[i]->dead) { win_destroy(g_wins[i]); i = -1; }
    desk_tick();
}

void wm_init(void) {
    g_sw = g_screen.w; g_sh = g_screen.h;
    g_cur_x = g_sw / 2; g_cur_y = g_sh / 2;
    desk_init();
    g_wm_ready = 1;
    wm_damage(0, 0, g_sw, g_sh);
}

/* ================= API para os aplicativos ================= */
void nx_win_title(nx_win *p, const char *t) {
    window_t *w = (window_t *)p;
    strncpy(w->title, t, 63); w->title[63] = 0;
    draw_chrome(w); damage_title(w);
    wm_damage(0, g_sh - TB_H, g_sw, TB_H);
}
void nx_win_invalidate_rect(nx_win *p, int x, int y, int ww, int hh) {
    window_t *w = (window_t *)p;
    if (w->minimized) return;
    w->need_paint = 1;                       /* o app repinta no proximo tick */
    wm_damage(w->x + 1 + x, w->y + TITLE_H + y, ww, hh);
}
void nx_win_invalidate(nx_win *p) {
    window_t *w = (window_t *)p;
    nx_win_invalidate_rect(p, 0, 0, w->pub.cv.w, w->pub.cv.h);
}
void nx_win_close(nx_win *p) { ((window_t *)p)->dead = 1; }
void nx_win_timer(nx_win *p, int ms) { window_t *w = (window_t *)p; w->timer_ms = ms; w->next_timer = g_ticks + ms; }
void nx_win_minsize(nx_win *p, int cw, int ch) { window_t *w = (window_t *)p; w->min_cw = cw; w->min_ch = ch; }

/* pinta imediatamente as janelas de um app recem-criadas (evita janela em branco) */
void wm_paint_app(int app) {
    for (int i = 0; i < g_nwin; i++) {
        window_t *w = g_wins[i];
        if (w->app != app || !w->need_paint || w->dead || !w->pub.on_paint) continue;
        w->need_paint = 0;
        nxe_set_current(app);
        w->pub.on_paint(&w->pub);
        nxe_set_current(0);
        wm_damage_win(w);
    }
}
