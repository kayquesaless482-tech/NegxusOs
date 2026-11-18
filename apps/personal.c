/* Personalizar: troca o papel de parede da area de trabalho */
#include "../sdk/apputil.h"
static nx_win *win;
static int cur = 0;
static const char *names[4] = { "Azul (Windows 10)", "Cinza escuro", "Verde-azulado", "Roxo" };
static const uint32_t prev[4][2] = { { RGB(0,60,130), RGB(0,120,215) }, { RGB(40,40,44), RGB(40,40,44) },
                                     { RGB(0,128,128), RGB(0,128,128) }, { RGB(70,30,110), RGB(150,60,150) } };

static void draw(nx_win *w) {
    nx_canvas *c = &w->cv;
    A->fill(c, 0, 0, c->w, c->h, RGB(255, 255, 255));
    A->text(c, 20, 16, "Papel de parede", NF_H3, RGB(0, 0, 0));
    for (int i = 0; i < 4; i++) {
        int x = 20 + (i % 2) * 200, y = 60 + (i / 2) * 150;
        A->fill(c, x - 2, y - 2, 164, 104, cur == i ? RGB(0, 120, 215) : RGB(200, 200, 200));
        A->fill(c, x, y, 160, 100, RGB(255, 255, 255));
        A->fill(c, x + 4, y + 4, 152, 92, prev[i][0]);
        A->text(c, x, y + 108, names[i], NF_UI, RGB(0, 0, 0));
    }
    A->text(c, 20, c->h - 30, "Clique em uma opcao para aplicar.", NF_SM, RGB(90, 90, 90));
}
static void on_paint(nx_win *w) { draw(w); }
static void on_mouse(nx_win *w, int type, int x, int y, int btn, int arg) {
    (void)btn; (void)arg;
    if (type != NMOUSE_DOWN) return;
    for (int i = 0; i < 4; i++) {
        int bx = 20 + (i % 2) * 200, by = 60 + (i / 2) * 150;
        if (nx_inside(bx, by, 160, 100, x, y)) {
            cur = i;
            A->set_wallpaper(i);
            char buf[16]; buf[0] = '0' + i; buf[1] = '\n'; buf[2] = 0;
            A->fs_mkdir("/Documentos");
            A->fs_write("/Documentos/papel.txt", buf, 2);
            A->win_invalidate(w);
            return;
        }
    }
}
int app_main(const nx_api *api, const char *arg) {
    (void)api; (void)arg;
    char b[8]; int n = A->fs_read("/Documentos/papel.txt", b, 4);
    if (n > 0 && b[0] >= '0' && b[0] <= '3') cur = b[0] - '0';
    win = A->win_create("Personalizar", 440, 430, NW_FIXED);
    if (!win) return -1;
    win->on_paint = on_paint; win->on_mouse = on_mouse;
    return 0;
}
