/* NegxusOS - estruturas internas do gerenciador de janelas e da area de trabalho */
#pragma once
#include "kernel.h"

#define MAXWIN   32
#define TITLE_H  34
#define TB_H     48
#define WIN_R    0            /* estilo classico: cantos retos */
#define NK_WIN   0x130
#define ABS_I(v) ((v) < 0 ? -(v) : (v))

typedef struct window {
    nx_win pub;                     /* deve ser o primeiro membro */
    int x, y, w, h;                 /* geometria externa (com moldura) */
    int rx, ry, rw, rh;             /* geometria para restaurar */
    u32 *surf;
    char title[64];
    int flags, maximized, minimized, app, hover_btn, dead;
    int timer_ms; u32 next_timer;
    int min_cw, min_ch;
    u32 icon[NXE_ICON_PX * NXE_ICON_PX];
    int has_icon;
    int serial;
    int need_paint;                  /* primeira pintura apos a criacao */
} window_t;

extern window_t *g_wins[MAXWIN];
extern int g_nwin;
extern window_t *g_active;
extern int g_sw, g_sh;
extern int g_cur_x, g_cur_y;

void wm_damage(int x, int y, int w, int h);
void wm_damage_win(window_t *w);
void wm_focus(window_t *w);
void wm_minimize(window_t *w);
void wm_restore(window_t *w);
void wm_close(window_t *w);
window_t *wm_find_app_window(int app);
const u8 *gfx_corner_cov(int r);

/* desktop.c */
void desk_init(void);
void desk_draw_background(nx_canvas *c);
void desk_draw_overlay(nx_canvas *c);          /* barra de tarefas, menus */
void desk_draw_cursor(nx_canvas *c);
int  desk_mouse(int type, int x, int y, int btn, int arg);   /* 1 = consumido */
int  desk_key(int key, int mods);                            /* 1 = consumido */
void desk_tick(void);
int  desk_menu_open(void);
int  desk_cursor_shape(void);
void desk_set_cursor_shape(int s);
void desk_hide_menus(void);
