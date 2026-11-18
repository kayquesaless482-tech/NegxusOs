/* Jogo da Cobrinha */
#include "../sdk/apputil.h"
#define GW 24
#define GH 18
#define CELL 24
#define DIRS 4
static nx_win *win;
static int sx[GW * GH], sy[GW * GH], len, dir, ndir, fx, fy, score, over, started, best;
static int alive_timer;
static const int dx[DIRS] = {1, 0, -1, 0}, dy[DIRS] = {0, 1, 0, -1};

static void place_food(void) {
    for (;;) {
        int x = A->rand() % GW, y = A->rand() % GH, ok = 1;
        for (int i = 0; i < len; i++) if (sx[i] == x && sy[i] == y) { ok = 0; break; }
        if (ok) { fx = x; fy = y; return; }
    }
}
static void reset(void) {
    len = 4; dir = ndir = 0; score = 0; over = 0; started = 0;
    for (int i = 0; i < len; i++) { sx[i] = 8 - i; sy[i] = 9; }
    place_food();
}
static void draw(nx_win *w) {
    nx_canvas *c = &w->cv;
    int ox = (c->w - GW * CELL) / 2, oy = 56 + (c->h - 56 - GH * CELL) / 2;
    A->fill(c, 0, 0, c->w, c->h, RGB(245, 247, 250));
    A->fill(c, 0, 0, c->w, 48, RGB(255, 255, 255));
    A->fill(c, 0, 47, c->w, 1, RGB(220, 224, 232));
    char b[64]; nx_snprintf(b, sizeof b, "Pontos: %d    Recorde: %d", score, best);
    A->text(c, 16, 14, b, NF_UIB, RGB(27, 27, 30));
    A->text(c, c->w - 330, 14, "Setas: mover   R: reiniciar", NF_UI, RGB(110, 110, 120));
    A->rrect(c, ox - 4, oy - 4, GW * CELL + 8, GH * CELL + 8, 10, RGB(30, 40, 60));
    A->rrect(c, ox, oy, GW * CELL, GH * CELL, 8, RGB(20, 30, 48));
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++)
        if (((x + y) & 1) == 0) A->fill(c, ox + x * CELL, oy + y * CELL, CELL, CELL, RGB(26, 38, 60));
    A->rrect(c, ox + fx * CELL + 5, oy + fy * CELL + 5, CELL - 10, CELL - 10, 6, RGB(255, 96, 96));
    for (int i = len - 1; i >= 0; i--) {
        uint32_t col = i == 0 ? RGB(120, 230, 150) : RGB(56, 178, 104);
        A->rrect(c, ox + sx[i] * CELL + 2, oy + sy[i] * CELL + 2, CELL - 4, CELL - 4, 7, col);
    }
    if (over) {
        A->rrect(c, c->w / 2 - 160, c->h / 2 - 50, 320, 96, 14, RGB(255, 255, 255));
        A->text(c, c->w / 2 - 70, c->h / 2 - 34, "Fim de jogo", NF_H2, RGB(27, 27, 30));
        A->text(c, c->w / 2 - 110, c->h / 2 + 4, "Pressione R para jogar de novo", NF_UI, RGB(110, 110, 120));
    } else if (!started) {
        A->text(c, c->w / 2 - 120, c->h / 2 - 10, "Pressione uma seta para começar", NF_UIB, RGB(255, 255, 255));
    }
}
static void on_paint(nx_win *w) { draw(w); }
static void step(nx_win *w) {
    if (!started || over) return;
    dir = ndir;
    int nx = sx[0] + dx[dir], ny = sy[0] + dy[dir];
    if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) { over = 1; }
    for (int i = 0; i < len && !over; i++) if (sx[i] == nx && sy[i] == ny) over = 1;
    if (!over) {
        int eat = (nx == fx && ny == fy);
        if (eat && len < GW * GH - 1) len++;
        for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
        sx[0] = nx; sy[0] = ny;
        if (eat) { score += 10; if (score > best) best = score; place_food(); }
    }
    A->win_invalidate(w);
}
static void on_timer(nx_win *w) { step(w); }
static void on_key(nx_win *w, int key, int mods) {
    (void)mods;
    if (key == 'r' || key == 'R') { reset(); started = 0; A->win_invalidate(w); return; }
    int nd = -1;
    if (key == NK_RIGHT) nd = 0; else if (key == NK_DOWN) nd = 1;
    else if (key == NK_LEFT) nd = 2; else if (key == NK_UP) nd = 3;
    if (nd < 0 || over) return;
    if (!started) { started = 1; dir = ndir = nd; step(w); return; }
    if ((nd + 2) % 4 == dir) return;                         /* nao reverter */
    ndir = nd;
}
static int on_close(nx_win *w) { (void)w; return 1; }
int app_main(const nx_api *api, const char *arg) {
    (void)api; (void)arg;
    best = 0; reset();
    win = A->win_create("Jogo da Cobrinha", GW * CELL + 40, GH * CELL + 96, NW_FIXED);
    if (!win) return -1;
    win->on_paint = on_paint; win->on_key = on_key; win->on_close = on_close; win->on_timer = on_timer;
    A->win_timer(win, 110);

    return 0;
}
