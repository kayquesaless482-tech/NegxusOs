/* NegxusOS - area de trabalho: papel de parede, barra de tarefas, menu Iniciar, icones, cursor */
#include "wm.h"

#define ACCENT     RGB(0, 103, 192)
#define MENU_BG    RGB(32, 32, 32)
#define HOVER      RGB(60, 60, 60)
#define TEXT       RGB(255, 255, 255)
#define TEXT_SUB   RGB(170, 170, 170)
#define BAR_BG     RGB(32, 32, 32)

#define ICON_W    96
#define ICON_H    92
#define START_W   340
#define START_H   460
#define BAR_H     TB_H
#define START_BTN 56
#define CTX_W     190

enum { MENU_NONE = 0, MENU_START, MENU_CTX };
static int menu;
static int menu_x, menu_y, menu_hover = -1, ctx_app_idx = -1;
static int cur_shape = NC_ARROW;
static int sel_icon = -1;
static u32 *wall;
int g_wall_id;
static int bar_min = -1;
static void draw_desk_icons(nx_canvas *c);
static char clock_txt[16];

/* ================= papel de parede ================= */
/* papeis de parede: 0 azul (Windows 10), 1 cinza escuro, 2 verde-azulado, 3 roxo */
static void build_wall(void) {
    int w = g_sw, h = g_sh;
    if (wall) { kfree(wall); wall = 0; }
    wall = kzalloc((size_t)w * h * 4);
    if (!wall) return;
    nx_canvas c = cv_make(wall, w, h, w);
    if (g_wall_id == 1) cv_fill(&c, 0, 0, w, h, RGB(40, 40, 44));
    else if (g_wall_id == 2) cv_fill(&c, 0, 0, w, h, RGB(0, 128, 128));
    else if (g_wall_id == 3) cv_gradient_v(&c, 0, 0, w, h, RGB(70, 30, 110), RGB(150, 60, 150));
    else cv_gradient_v(&c, 0, 0, w, h, RGB(0, 60, 130), RGB(0, 120, 215));
}
void desk_set_wallpaper(int id) {
    if (id < 0 || id > 3) return;
    g_wall_id = id;
    build_wall();
    wm_damage(0, 0, g_sw, g_sh);
}

void desk_init(void) {
    build_wall();
    menu = MENU_NONE;
}

void desk_draw_background(nx_canvas *c) {
    if (wall) {
        nx_canvas s = cv_make(wall, g_sw, g_sh, g_sw);
        cv_blit(c, 0, 0, &s, 0, 0, g_sw, g_sh);
    } else cv_fill(c, 0, 0, g_sw, g_sh, RGB(30, 80, 150));
    draw_desk_icons(c);
}

/* ================= icones da area de trabalho ================= */
static void icon_pos(int i, int *x, int *y) {
    int col = i / 7, row = i % 7;
    *x = 14 + col * ICON_W; *y = 14 + row * ICON_H;
}
static int is_desk_app(const app_info_t *a) {
    return (a->flags & NXE_F_DESKTOP) && !(a->flags & NXE_F_HIDDEN);
}
static void draw_desk_icons(nx_canvas *c) {
    int n = nxe_app_count();
    for (int i = 0; i < n; i++) {
        const app_info_t *a = nxe_app(i);
        if (!is_desk_app(a)) continue;
        int x, y; icon_pos(i, &x, &y);
        if (sel_icon == i) cv_rrect_a(c, x + 6, y + 2, ICON_W - 12, ICON_H - 6, 8, ACCENT, 110);
        cv_icon(c, a->icon, x + (ICON_W - 44) / 2, y + 6, 44);
        int tw = text_width(a->name, NF_UI);
        cv_text(c, x + (ICON_W - tw) / 2, y + 58, a->name, NF_UI, RGB(255, 255, 255));
    }
}
static int icon_at(int mx, int my) {
    int n = nxe_app_count();
    for (int i = 0; i < n; i++) {
        const app_info_t *a = nxe_app(i);
        if (!is_desk_app(a)) continue;
        int x, y; icon_pos(i, &x, &y);
        if (mx >= x && mx < x + ICON_W && my >= y && my < y + ICON_H) return i;
    }
    return -1;
}

/* ================= barra de tarefas ================= */
static void draw_clock(nx_canvas *c) {
    nx_time t; rtc_read(&t);
    nx_snprintf(clock_txt, sizeof clock_txt, "%02d:%02d", t.hour, t.min);
    char date[16];
    nx_snprintf(date, sizeof date, "%02d/%02d/%d", t.day, t.month, t.year);
    int x = g_sw - 92, by = g_sh - BAR_H;
    cv_text(c, x + (92 - text_width(clock_txt, NF_UI)) / 2, by + 6, clock_txt, NF_UI, TEXT);
    cv_text(c, x + (92 - text_width(date, NF_SM)) / 2, by + 24, date, NF_SM, TEXT_SUB);
}

extern const u32 g_logo40[2304];
static void draw_logo(nx_canvas *c, int x, int y) {
    nx_canvas lg = cv_make((u32 *)g_logo40, 40, 40, 40);
    cv_icon(c, g_logo40, x, y, 26);
    (void)lg;
}

void desk_draw_overlay(nx_canvas *c) {
    int by = g_sh - BAR_H;
    cv_fill(c, 0, by, g_sw, BAR_H, BAR_BG);
    cv_fill(c, 0, by, g_sw, 1, RGB(60, 60, 60));
    if (menu == MENU_START) cv_fill(c, 4, by + 4, START_BTN - 8, BAR_H - 8, RGB(70, 70, 70));
    draw_logo(c, 4 + (START_BTN - 8 - 20) / 2, by + (BAR_H - 20) / 2);

    int x = START_BTN + 6;
    for (int i = 0; i < g_nwin; i++) {
        window_t *w = g_wins[i];
        if (w->dead) continue;
        if (x + 150 > g_sw - 100) break;
        int act = (g_active == w && !w->minimized);
        cv_fill(c, x, by + 4, 150, BAR_H - 8, act ? RGB(70, 70, 70) : RGB(50, 50, 50));
        if (act) cv_fill(c, x, by + BAR_H - 4, 150, 3, RGB(0, 120, 215));
        int tx = x + 12;
        if (w->has_icon) { cv_icon(c, w->icon, tx, by + (BAR_H - 18) / 2, 18); tx += 26; }
        char tb[32]; strncpy(tb, w->title, 20); tb[20] = 0;
        cv_text(c, tx, by + (BAR_H - text_height(NF_UI)) / 2, tb, NF_UI, w->minimized ? TEXT_SUB : TEXT);
        x += 154;
    }
    draw_clock(c);

    if (menu == MENU_START) {
        cv_fill(c, menu_x, menu_y, START_W, START_H, MENU_BG);
        cv_rect(c, menu_x, menu_y, START_W, START_H, RGB(60, 60, 60));
        cv_text(c, menu_x + 18, menu_y + 16, "Aplicativos", NF_H3, TEXT);
        int n = nxe_app_count(), row = 0;
        for (int i = 0; i < n && row < 8; i++) {
            const app_info_t *a = nxe_app(i);
            if (a->flags & NXE_F_HIDDEN) continue;
            int ry = menu_y + 48 + row * 44;
            if (menu_hover == i) cv_fill(c, menu_x + 8, ry, START_W - 16, 40, HOVER);
            cv_icon(c, a->icon, menu_x + 16, ry + 4, 32);
            cv_text(c, menu_x + 60, ry + 12, a->name, NF_UI, TEXT);
            row++;
        }
        int fy = menu_y + START_H - 50;
        cv_fill(c, menu_x + 14, fy - 6, START_W - 28, 1, RGB(220, 222, 230));
        if (menu_hover == 1000) cv_fill(c, menu_x + 10, fy, 150, 36, HOVER);
        if (menu_hover == 1001) cv_fill(c, menu_x + 170, fy, 150, 36, HOVER);
        cv_text(c, menu_x + 26, fy + 10, "Reiniciar", NF_UI, TEXT);
        cv_text(c, menu_x + 186, fy + 10, "Desligar", NF_UI, TEXT);
    }
    if (menu == MENU_CTX) {
        int ch = 88;
        cv_fill(c, menu_x, menu_y, CTX_W, ch, MENU_BG);
        cv_rect(c, menu_x, menu_y, CTX_W, ch, RGB(60, 60, 60));
        if (menu_hover == 0) cv_fill(c, menu_x + 4, menu_y + 4, CTX_W - 8, 32, HOVER);
        if (menu_hover == 1) cv_fill(c, menu_x + 4, menu_y + 42, CTX_W - 8, 32, HOVER);
        cv_text(c, menu_x + 18, menu_y + 15, ctx_app_idx >= 0 ? "Abrir" : "Atualizar", NF_UI, TEXT);
        cv_text(c, menu_x + 18, menu_y + 53, ctx_app_idx >= 0 ? "Executar como janela" : "Ver aplicativos", NF_UI, TEXT);
    }
}

void desk_draw_cursor(nx_canvas *c) {
    int x = g_cur_x, y = g_cur_y;
    static const char *shape[] = {
        "X          ", "XX         ", "X.X        ", "X..X       ", "X...X      ",
        "X....X     ", "X.....X    ", "X......X   ", "X.......X  ", "X........X ",
        "X.....XXXXX", "X..X..X    ", "X.X X..X   ", "XX  X..X   ", "    X..X   ",
        "     X..X  ", "     XX   "
    };
    for (int j = 0; j < 17; j++) for (int i = 0; i < 11; i++) {
        char ch = shape[j][i];
        if (ch == 'X') cv_fill(c, x + i, y + j, 1, 1, RGB(0, 0, 0));
        else if (ch == '.') cv_fill(c, x + i, y + j, 1, 1, RGB(255, 255, 255));
    }
}

/* ================= menus e entrada ================= */
void desk_hide_menus(void) { menu = MENU_NONE; menu_hover = -1; }
int desk_menu_open(void) { return menu != MENU_NONE; }
int desk_cursor_shape(void) { return cur_shape; }
void desk_set_cursor_shape(int s) { cur_shape = s; }

static void open_start(void) {
    menu = MENU_START; menu_hover = -1;
    menu_x = 6; menu_y = g_sh - BAR_H - START_H - 8;
    wm_damage(0, 0, g_sw, g_sh);
}
static void open_ctx(int x, int y, int app_idx) {
    menu = MENU_CTX; ctx_app_idx = app_idx; menu_hover = -1;
    menu_x = CLAMP(x, 0, g_sw - CTX_W - 4); menu_y = CLAMP(y, 0, g_sh - BAR_H - 100);
    wm_damage(0, 0, g_sw, g_sh);
}
static int start_hit_app(int mx, int my) {
    if (mx < menu_x + 8 || mx >= menu_x + START_W - 8 || my < menu_y + 48) return -1;
    int row = (my - (menu_y + 48)) / 44, n = nxe_app_count(), r = 0;
    for (int i = 0; i < n; i++) {
        if (nxe_app(i)->flags & NXE_F_HIDDEN) continue;
        if (r == row) return i;
        r++;
    }
    return -1;
}
static void run_app(int idx) {
    const app_info_t *a = nxe_app(idx);
    if (a) nxe_exec(a->path, 0);
}

int desk_mouse(int type, int x, int y, int btn, int arg) {
    (void)arg;
    int by = g_sh - BAR_H;
    if (type == NMOUSE_MOVE) {
        int h = -1;
        if (menu == MENU_START && x >= menu_x && x < menu_x + START_W && y >= menu_y && y < menu_y + START_H) {
            h = start_hit_app(x, y);
            int fy = menu_y + START_H - 50;
            if (h < 0 && y >= fy && y < fy + 36) h = (x < menu_x + 160) ? 1000 : 1001;
        } else if (menu == MENU_CTX && x >= menu_x && x < menu_x + CTX_W && y >= menu_y && y < menu_y + 88) {
            h = (y < menu_y + 42) ? 0 : 1;
        }
        if (h != menu_hover) { menu_hover = h; wm_damage(0, 0, g_sw, g_sh); }
        return 0;
    }
    if (type == NMOUSE_DBL) {
        if (menu == MENU_NONE && y < by) { int ic = icon_at(x, y); if (ic >= 0) { run_app(ic); return 1; } }
        return 0;
    }
    if (type != NMOUSE_DOWN) return 0;

    if (menu == MENU_START) {
        if (x >= menu_x && x < menu_x + START_W && y >= menu_y && y < menu_y + START_H) {
            int ai = start_hit_app(x, y);
            if (ai >= 0) { desk_hide_menus(); run_app(ai); return 1; }
            if (menu_hover == 1000) { desk_hide_menus(); system_reboot(); }
            if (menu_hover == 1001) { desk_hide_menus(); system_poweroff(); }
            return 1;
        }
        desk_hide_menus(); wm_damage(0, 0, g_sw, g_sh);
        if (y >= by && x < START_BTN) return 1;
    } else if (menu == MENU_CTX) {
        int inside = x >= menu_x && x < menu_x + CTX_W && y >= menu_y && y < menu_y + 88;
        desk_hide_menus(); wm_damage(0, 0, g_sw, g_sh);
        if (inside) {
            if (ctx_app_idx >= 0 && y < menu_y + 42) { run_app(ctx_app_idx); }
            else if (ctx_app_idx < 0 && y >= menu_y + 42) { }
            return 1;
        }
    }
    if (y >= by) {
        if (x < START_BTN) { open_start(); return 1; }
        int xx = START_BTN + 6;
        for (int i = 0; i < g_nwin; i++) {
            window_t *w = g_wins[i];
            if (w->dead) continue;
            if (xx + 150 > g_sw - 100) break;
            if (x >= xx && x < xx + 150) {
                if (w->minimized) wm_restore(w);
                else if (g_active == w) wm_minimize(w);
                else wm_focus(w);
                return 1;
            }
            xx += 154;
        }
        return 1;
    }
    int ic = icon_at(x, y);
    if (ic >= 0) {
        if (sel_icon != ic) { sel_icon = ic; wm_damage(0, 0, g_sw, by); }
        if (btn & NB_RIGHT) open_ctx(x, y, ic);
        return 1;
    }
    if (sel_icon >= 0) { sel_icon = -1; wm_damage(0, 0, g_sw, by); }
    if (btn & NB_RIGHT) { open_ctx(x, y, -1); return 1; }
    return 0;
}

int desk_key(int key, int mods) {
    if (key == NK_ESC && menu != MENU_NONE) { desk_hide_menus(); wm_damage(0, 0, g_sw, g_sh); return 1; }
    if ((mods & NM_CTRL) && key == NK_ESC) { open_start(); return 1; }
    if (key == NK_ENTER && menu == MENU_NONE && sel_icon >= 0 && !g_active) { run_app(sel_icon); return 1; }
    return 0;
}

void desk_tick(void) {
    nx_time t; rtc_read(&t);
    if (t.min != bar_min) { bar_min = t.min; wm_damage(g_sw - 92, g_sh - BAR_H, 92, BAR_H); }
}
