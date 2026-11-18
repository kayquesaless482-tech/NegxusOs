/* Campo Minado */
#include "../sdk/apputil.h"
#define W 12
#define H 10
#define MINES 16
#define CS 36
static nx_win *win;
static unsigned char mine[W * H], open_[W * H], flag[W * H];
static unsigned char num[W * H];
static int state, flags, secs, first, wins_total;
static int best_time;
static int hovx = -1, hovy = -1;

static void newgame(void) {
    for (int i = 0; i < W * H; i++) { mine[i] = open_[i] = flag[i] = num[i] = 0; }
    state = 0; flags = 0; secs = 0; first = 1;
}
static void place(int sx0, int sy0) {
    int placed = 0;
    while (placed < MINES) {
        int i = A->rand() % (W * H), x = i % W, y = i / W;
        if (mine[i] || (nx_iabs(x - sx0) <= 1 && nx_iabs(y - sy0) <= 1)) continue;
        mine[i] = 1; placed++;
    }
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int n = 0;
        for (int yy = -1; yy <= 1; yy++) for (int xx = -1; xx <= 1; xx++) {
            int nx = x + xx, ny = y + yy;
            if (nx >= 0 && ny >= 0 && nx < W && ny < H && mine[ny * W + nx]) n++;
        }
        num[y * W + x] = n;
    }
    first = 0;
}
static void flood(int x, int y) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    int i = y * W + x;
    if (open_[i] || flag[i]) return;
    open_[i] = 1;
    if (num[i] == 0 && !mine[i]) for (int yy = -1; yy <= 1; yy++) for (int xx = -1; xx <= 1; xx++) flood(x + xx, y + yy);
}
static int check_win(void) {
    for (int i = 0; i < W * H; i++) if (!mine[i] && !open_[i]) return 0;
    return 1;
}
static void reveal_all(void) { for (int i = 0; i < W * H; i++) if (mine[i]) open_[i] = 1; }
static int ox(nx_win *w) { return (w->cv.w - W * CS) / 2; }
static int oy(nx_win *w) { return 64 + (w->cv.h - 64 - H * CS) / 2; }

static void draw(nx_win *w) {
    nx_canvas *c = &w->cv;
    A->fill(c, 0, 0, c->w, c->h, RGB(240, 242, 246));
    A->fill(c, 0, 0, c->w, 56, RGB(255, 255, 255));
    char b[80];
    nx_snprintf(b, sizeof b, "Minas: %d", MINES - flags);
    A->text(c, 20, 18, b, NF_UIB, RGB(27, 27, 30));
    nx_snprintf(b, sizeof b, "Tempo: %d s", secs);
    A->text(c, c->w - 130, 18, b, NF_UIB, RGB(27, 27, 30));
    const char *face = state == 2 ? "Perdeu" : state == 3 ? "Venceu!" : "Clique para jogar";
    A->text(c, c->w / 2 - CW(face) / 2, 18, face, NF_UIB, state == 2 ? RGB(196, 43, 28) : state == 3 ? RGB(16, 124, 16) : RGB(90, 90, 100));
    int X = ox(w), Y = oy(w);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int i = y * W + x, px = X + x * CS, py = Y + y * CS;
        if (open_[i]) {
            A->fill(c, px, py, CS, CS, RGB(230, 232, 238));
            A->rect(c, px, py, CS, CS, RGB(206, 210, 220));
            if (mine[i]) {
                A->circle(c, px + CS / 2, py + CS / 2, 8, RGB(30, 30, 35));
            } else if (num[i]) {
                static const uint32_t col[9] = {0,RGB(25,118,210),RGB(56,142,60),RGB(211,47,47),RGB(81,45,168),RGB(123,31,62),RGB(0,131,143),RGB(33,33,33),RGB(120,120,120)};
                char d[2] = { '0' + num[i], 0 };
                A->text(c, px + CS / 2 - CW(d) / 2, py + 9, d, NF_UIB, col[num[i]]);
            }
        } else {
            int hov = (x == hovx && y == hovy && state < 2);
            A->rrect(c, px, py, CS, CS, 4, hov ? RGB(250, 250, 255) : RGB(255, 255, 255));
            A->rrect_line(c, px, py, CS, CS, 4, RGB(176, 182, 196));
            if (flag[i]) {
                A->fill(c, px + 14, py + 8, 2, 20, RGB(40, 40, 40));
                A->fill(c, px + 16, py + 8, 10, 7, RGB(220, 40, 40));
            }
        }
    }
}
static void on_paint(nx_win *w) { draw(w); }
static void on_timer(nx_win *w) { if (state == 1 && secs < 999) { secs++; A->win_invalidate_rect(w, 0, 0, w->cv.w, 56); } }
static void on_mouse(nx_win *w, int type, int mx, int my, int btn, int arg) {
    (void)arg;
    int X = ox(w), Y = oy(w);
    int gx = (mx - X) / CS, gy = (my - Y) / CS;
    int inb = mx >= X && my >= Y && gx >= 0 && gy >= 0 && gx < W && gy < H;
    if (type == NMOUSE_MOVE) {
        int nh = inb ? gx : -1, nv = inb ? gy : -1;
        if (nh != hovx || nv != hovy) { hovx = nh; hovy = nv; A->win_invalidate(w); }
        w->cursor = inb ? NC_HAND : NC_ARROW;
        return;
    }
    if (type != NMOUSE_DOWN || !inb || state >= 2) {
        if (type == NMOUSE_DOWN && my < 56) { newgame(); A->win_invalidate(w); }
        return;
    }
    int i = gy * W + gx;
    if (btn & NB_RIGHT) {
        if (!open_[i]) { flag[i] = !flag[i]; flags += flag[i] ? 1 : -1; A->win_invalidate(w); }
        return;
    }
    if (btn & NB_LEFT) {
        if (flag[i] || open_[i]) return;
        if (first) { place(gx, gy); state = 1; }
        if (mine[i]) { reveal_all(); state = 2; A->win_invalidate(w); return; }
        flood(gx, gy);
        if (check_win()) { state = 3; for (int k = 0; k < W * H; k++) if (mine[k]) flag[k] = 1; }
        A->win_invalidate(w);
    }
}
static void on_key(nx_win *w, int key, int mods) { (void)mods; if (key == 'n' || key == 'N' || key == 13) { newgame(); A->win_invalidate(w); } }
static void on_resize(nx_win *w) { (void)w; }
int app_main(const nx_api *api, const char *arg) {
    (void)api; (void)arg;
    newgame(); (void)best_time; (void)wins_total;
    win = A->win_create("Campo Minado", W * CS + 40, H * CS + 88, NW_FIXED);
    if (!win) return -1;
    win->on_paint = on_paint; win->on_mouse = on_mouse; win->on_key = on_key; win->on_timer = on_timer;
    win->on_resize = on_resize;
    A->win_timer(win, 1000);
    return 0;
}
