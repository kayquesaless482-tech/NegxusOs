/* Explorador de Arquivos */
#include "../sdk/apputil.h"
#define MAXE 96
#define ROW 30
static nx_win *win;
static char path[128] = "/";
static nx_dirent ents[MAXE];
static int nents, sel = -1, scroll, hover = -1;
static char status[96];
static const char *quick[] = { "/", "/Apps", "/Documentos", "/Imagens" };
static const char *quick_name[] = { "Raiz", "Aplicativos", "Documentos", "Imagens" };
#define SIDE 170
#define TOP 40

static void load_dir(void) {
    nents = A->fs_list(path, ents, MAXE);
    if (nents < 0) { nents = 0; strcpy(status, "Pasta nao encontrada"); }
    else nx_snprintf(status, sizeof status, "%d itens", nents);
    sel = -1; scroll = 0;
}
static void go(const char *p) { strncpy(path, p, 127); path[127] = 0; load_dir(); }
static void up(void) {
    if (!strcmp(path, "/")) return;
    char *s = strrchr(path, '/');
    if (s == path) strcpy(path, "/"); else *s = 0;
    load_dir();
}
static void enter_item(int i) {
    if (i < 0 || i >= nents) return;
    char full[200];
    if (!strcmp(path, "/")) nx_snprintf(full, sizeof full, "/%s", ents[i].name);
    else nx_snprintf(full, sizeof full, "%s/%s", path, ents[i].name);
    if (ents[i].is_dir) { go(full); return; }
    int r = A->open(full);
    if (r < 0) strcpy(status, r == -2 ? "Nenhum aplicativo para este tipo" : "Nao foi possivel abrir");
}
#define GRAY RGB(192,192,192)
static void bevel_sunken(nx_canvas *c, int x, int y, int w, int h) {
    A->fill(c, x, y, w, 1, RGB(128,128,128)); A->fill(c, x, y, 1, h, RGB(128,128,128));
    A->fill(c, x, y + h - 1, w, 1, RGB(255,255,255)); A->fill(c, x + w - 1, y, 1, h, RGB(255,255,255));
}
static void bevel_raised(nx_canvas *c, int x, int y, int w, int h) {
    A->fill(c, x, y, w, 1, RGB(255,255,255)); A->fill(c, x, y, 1, h, RGB(255,255,255));
    A->fill(c, x, y + h - 1, w, 1, RGB(64,64,64)); A->fill(c, x + w - 1, y, 1, h, RGB(64,64,64));
}
static void draw(nx_win *w) {
    nx_canvas *c = &w->cv;
    int W = c->w, H = c->h;
    A->fill(c, 0, 0, W, H, GRAY);
    /* barra de endereco (Windows 95: caixa afundada) */
    A->text(c, 8, 12, "Caminho:", NF_SM, RGB(0,0,0));
    A->fill(c, 70, 8, W - 82, 22, RGB(255,255,255));
    bevel_sunken(c, 70, 8, W - 82, 22);
    A->text(c, 76, 12, path, NF_UI, RGB(0,0,0));
    /* painel de pastas rapidas (esquerda) */
    A->fill(c, 8, TOP, SIDE - 16, H - TOP - 30, RGB(255,255,255));
    bevel_sunken(c, 8, TOP, SIDE - 16, H - TOP - 30);
    for (int i = 0; i < 4; i++) {
        int y = TOP + 8 + i * 26, act = !strcmp(path, quick[i]);
        if (act) A->fill(c, 12, y, SIDE - 24, 22, RGB(0,0,128));
        A->text(c, 22, y + 4, quick_name[i], NF_UI, act ? RGB(255,255,255) : RGB(0,0,0));
    }
    /* cabecalho de colunas (botoes em relevo) */
    int hy = TOP, hw = W - SIDE - 16;
    int xn = SIDE + 40, xt = W - 260, xs = W - 130;
    A->fill(c, SIDE + 8, hy, hw, 20, GRAY);
    bevel_raised(c, SIDE + 8, hy, hw, 20);
    A->text(c, xn, hy + 4, "Nome", NF_UI, RGB(0,0,0));
    A->text(c, xt, hy + 4, "Tipo", NF_UI, RGB(0,0,0));
    A->text(c, xs, hy + 4, "Tamanho", NF_UI, RGB(0,0,0));
    /* lista (branca, afundada) */
    int lx = SIDE + 8, ly = TOP + 20, lw = hw, lh = H - ly - 30;
    A->fill(c, lx, ly, lw, lh, RGB(255,255,255));
    bevel_sunken(c, lx, ly, lw, lh);
    int rows = (lh - 4) / ROW, y0 = ly + 4;
    for (int r = 0; r < rows; r++) {
        int i = scroll + r;
        if (i >= nents) break;
        int y = y0 + r * ROW;
        if (i == sel) A->fill(c, lx + 2, y, lw - 4, ROW - 2, RGB(0,0,128));
        int tc = (i == sel) ? RGB(255,255,255) : RGB(0,0,0);
        if (ents[i].is_dir) {
            A->fill(c, SIDE + 16, y + 7, 14, 10, RGB(255,220,80));
            A->fill(c, SIDE + 16, y + 5, 6, 3, RGB(230,190,60));
        } else {
            A->fill(c, SIDE + 16, y + 4, 12, 16, RGB(255,255,255));
            A->rect(c, SIDE + 16, y + 4, 12, 16, RGB(0,0,0));
        }
        A->text(c, xn, y + 6, ents[i].name, NF_UI, tc);
        const char *ty = ents[i].is_dir ? "Pasta" : "Arquivo";
        A->text(c, xt, y + 6, ty, NF_UI, tc);
        if (!ents[i].is_dir) {
            char sz[24];
            if (ents[i].size < 1024) nx_snprintf(sz, sizeof sz, "%u B", ents[i].size);
            else nx_snprintf(sz, sizeof sz, "%u KB", ents[i].size / 1024);
            A->text(c, xs, y + 6, sz, NF_UI, tc);
        }
    }
    /* barra de status */
    A->fill(c, 8, H - 26, W - 16, 20, GRAY);
    bevel_sunken(c, 8, H - 26, W - 16, 20);
    A->text(c, 14, H - 22, status, NF_SM, RGB(0,0,0));
}
static void on_paint(nx_win *w) { draw(w); }
static void on_mouse(nx_win *w, int type, int mx, int my, int btn, int arg) {
    int rows = (w->cv.h - TOP - 50) / ROW, y0 = TOP + 24;
    if (type == NMOUSE_WHEEL) {
        scroll -= arg * 2;
        if (scroll < 0) scroll = 0;
        if (scroll > nents - 1) scroll = nents > 0 ? nents - 1 : 0;
        A->win_invalidate(w); return;
    }
    if (type == NMOUSE_MOVE) {
        int h = -1;
        if (mx > SIDE && my >= y0 && my < y0 + rows * ROW) h = scroll + (my - y0) / ROW;
        if (h >= nents) h = -1;
        if (h != hover) { hover = h; A->win_invalidate(w); }
        return;
    }
    if (type == NMOUSE_DOWN) {
        if (nx_inside(SIDE + 12, 14, 34, 28, mx, my)) { up(); A->win_invalidate(w); return; }
        for (int i = 0; i < 4; i++) if (nx_inside(8, 40 + i * 34, SIDE - 16, 28, mx, my)) { go(quick[i]); A->win_invalidate(w); return; }
        if (mx > SIDE && my >= y0 && my < y0 + rows * ROW) {
            int i = scroll + (my - y0) / ROW;
            if (i < nents) { sel = i; A->win_invalidate(w); }
            if (btn & NB_RIGHT) { /* menu de contexto simples: nada */ }
        }
        return;
    }
    if (type == NMOUSE_DBL) {
        if (mx > SIDE && my >= y0) {
            int i = scroll + (my - y0) / ROW;
            if (i < nents) { enter_item(i); A->win_invalidate(w); }
        }
    }
    (void)btn; (void)arg;
}
static void on_key(nx_win *w, int key, int mods) {
    (void)mods;
    if (key == NK_UP && sel > 0) sel--;
    else if (key == NK_DOWN && sel < nents - 1) sel++;
    else if (key == NK_ENTER && sel >= 0) enter_item(sel);
    else if (key == NK_BACKSPACE) up();
    else return;
    A->win_invalidate(w);
}
int app_main(const nx_api *api, const char *arg) {
    (void)api;
    A->fs_mkdir("/Documentos"); A->fs_mkdir("/Imagens");
    load_dir();
    win = A->win_create("Explorador de Arquivos", 860, 540, 0);
    if (!win) return -1;
    win->on_paint = on_paint; win->on_mouse = on_mouse; win->on_key = on_key;
    if (arg && arg[0]) go(arg);
    return 0;
}
