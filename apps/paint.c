/* Paint: desenho livre, formas, balde, borracha, salvar/abrir (.npt) */
#include "../sdk/apputil.h"
#define CW_ 720
#define CH_ 470
#define TOP 62
enum { T_PEN, T_BRUSH, T_ERASER, T_LINE, T_RECT, T_ELLIPSE, T_FILL, T_COUNT };
static const char *tool_name[T_COUNT] = { "Lapis", "Pincel", "Borracha", "Linha", "Retangulo", "Elipse", "Balde" };
static const uint32_t palette[12] = {
    RGB(0,0,0), RGB(90,90,90), RGB(160,0,0), RGB(230,30,30), RGB(255,130,0), RGB(255,220,0),
    RGB(0,150,60), RGB(0,190,190), RGB(0,90,220), RGB(120,50,200), RGB(255,120,200), RGB(255,255,255) };
static nx_win *win;
static uint32_t *cv;                 /* pixels do desenho */
static uint32_t *snap;               /* copia para pre-visualizar formas */
static int tool, color_i, size_i = 1, drawing, sx, sy, lx, ly;
static const int sizes[3] = { 1, 4, 9 };
static char msg[96];
static uint32_t msg_t;

static uint32_t cur(void) { return palette[color_i]; }
static void put(int x, int y, uint32_t col, int r) {
    for (int j = -r / 2; j <= r / 2; j++) for (int i = -r / 2; i <= r / 2; i++) {
        int px = x + i, py = y + j;
        if (px >= 0 && py >= 0 && px < CW_ && py < CH_) cv[py * CW_ + px] = col;
    }
}
static void line_px(int x0, int y0, int x1, int y1, uint32_t col, int r) {
    int dx = nx_iabs(x1 - x0), sxd = x0 < x1 ? 1 : -1, dy = -nx_iabs(y1 - y0), syd = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        put(x0, y0, col, r);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sxd; }
        if (e2 <= dx) { err += dx; y0 += syd; }
    }
}
static int isq(long long v) {
    if (v <= 0) return 0;
    long long r = 0, b = 1LL << 30;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return (int)r;
}
static void ellipse(int x0, int y0, int x1, int y1, uint32_t col, int r) {
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int rx = nx_iabs(x1 - x0) / 2, ry = nx_iabs(y1 - y0) / 2;
    if (rx < 1 || ry < 1) return;
    long long RX2 = (long long)rx * rx, RY2 = (long long)ry * ry;
    for (int dx = -rx; dx <= rx; dx++) {
        int dy = isq(RY2 * (RX2 - (long long)dx * dx) / RX2);
        put(cx + dx, cy + dy, col, r); put(cx + dx, cy - dy, col, r);
    }
    for (int dy = -ry; dy <= ry; dy++) {
        int dx = isq(RX2 * (RY2 - (long long)dy * dy) / RY2);
        put(cx + dx, cy + dy, col, r); put(cx - dx, cy + dy, col, r);
    }
}
static void fill_at(int x, int y, uint32_t nc) {
    if (x < 0 || y < 0 || x >= CW_ || y >= CH_) return;
    uint32_t oc = cv[y * CW_ + x];
    if (oc == nc) return;
    static int stk[CW_ * CH_ * 2];              /* pilha de pixels (x,y) */
    int sp = 0;
    stk[sp++] = x; stk[sp++] = y;
    while (sp) {
        int py = stk[--sp], px = stk[--sp];
        if (px < 0 || py < 0 || px >= CW_ || py >= CH_) continue;
        if (cv[py * CW_ + px] != oc) continue;
        cv[py * CW_ + px] = nc;
        if (sp + 8 >= CW_ * CH_ * 2) continue;
        stk[sp++] = px + 1; stk[sp++] = py;
        stk[sp++] = px - 1; stk[sp++] = py;
        stk[sp++] = px; stk[sp++] = py + 1;
        stk[sp++] = px; stk[sp++] = py - 1;
    }
}
static void preview(int x, int y) {
    memcpy(cv, snap, CW_ * CH_ * 4);
    int r = sizes[size_i];
    if (tool == T_LINE) line_px(sx, sy, x, y, cur(), r);
    else if (tool == T_RECT) {
        int x0 = nx_imin(sx, x), x1 = nx_imax(sx, x), y0 = nx_imin(sy, y), y1 = nx_imax(sy, y);
        for (int i = x0; i <= x1; i++) { put(i, y0, cur(), r); put(i, y1, cur(), r); }
        for (int j = y0; j <= y1; j++) { put(x0, j, cur(), r); put(x1, j, cur(), r); }
    } else if (tool == T_ELLIPSE) ellipse(sx, sy, x, y, cur(), r);
}
static void say(const char *s) { strncpy(msg, s, 95); msg[95] = 0; msg_t = 0; }

static void save(void) {
    int n = 12 + CW_ * CH_ * 4;
    uint8_t *b = A->malloc(n);
    if (!b) { say("Sem memoria para salvar"); return; }
    memcpy(b, "NPT1", 4);
    int w = CW_, h = CH_; memcpy(b + 4, &w, 4); memcpy(b + 8, &h, 4);
    memcpy(b + 12, cv, CW_ * CH_ * 4);
    A->fs_mkdir("/Documentos");
    int r = A->fs_write("/Documentos/desenho.npt", b, n);
    A->free(b);
    say(r > 0 ? "Salvo em Documentos/desenho.npt" : "Erro ao salvar");
}
static void load(const char *path) {
    nx_stat st;
    if (A->fs_stat(path, &st) < 0 || st.is_dir) { say("Arquivo nao encontrado"); return; }
    uint8_t *b = A->malloc(st.size + 1);
    if (!b) return;
    int n = A->fs_read(path, b, st.size);
    if (n > 12 && !memcmp(b, "NPT1", 4)) {
        int w, h; memcpy(&w, b + 4, 4); memcpy(&h, b + 8, 4);
        if (w == CW_ && h == CH_ && n >= 12 + CW_ * CH_ * 4) { memcpy(cv, b + 12, CW_ * CH_ * 4); say("Desenho aberto"); }
        else say("Formato de imagem nao suportado");
    } else say("Nao e um desenho do Paint");
    A->free(b);
    memcpy(snap, cv, CW_ * CH_ * 4);
}

static void draw(nx_win *w) {
    nx_canvas *c = &w->cv;
    A->fill(c, 0, 0, c->w, c->h, RGB(236, 238, 243));
    A->fill(c, 0, 0, c->w, TOP, RGB(255, 255, 255));
    A->fill(c, 0, TOP - 1, c->w, 1, RGB(214, 218, 228));
    for (int t = 0; t < T_COUNT; t++) {
        int x = 10 + t * 92, act = t == tool;
        A->rrect(c, x, 8, 86, 26, 7, act ? RGB(0, 103, 192) : RGB(238, 240, 246));
        A->text(c, x + 43 - CW(tool_name[t]) / 2, 13, tool_name[t], NF_UI, act ? RGB(255, 255, 255) : RGB(40, 40, 46));
    }
    for (int i = 0; i < 3; i++) {
        int x = 10 + i * 40 + 0, y = 38;
        A->rrect(c, x, y, 34, 18, 5, size_i == i ? RGB(200, 216, 240) : RGB(238, 240, 246));
        A->circle(c, x + 17, y + 9, sizes[i] / 2 + 1, RGB(40, 40, 46));
    }
    for (int i = 0; i < 12; i++) {
        int x = 10 + 3 * 40 + 20 + i * 24, y = 36;
        A->rrect(c, x, y, 20, 20, 4, palette[i]);
        A->rrect_line(c, x, y, 20, 20, 4, i == color_i ? RGB(0, 103, 192) : RGB(190, 196, 210));
    }
    int bx = c->w - 210;
    A->rrect(c, bx, 8, 90, 26, 7, RGB(238, 240, 246));
    A->text(c, bx + 45 - CW("Limpar") / 2, 13, "Limpar", NF_UI, RGB(40, 40, 46));
    A->rrect(c, bx + 100, 8, 100, 26, 7, RGB(0, 103, 192));
    A->text(c, bx + 150 - CW("Salvar") / 2, 13, "Salvar", NF_UI, RGB(255, 255, 255));
    int ox = (c->w - CW_) / 2, oy = TOP + 10;
    A->fill(c, ox - 2, oy - 2, CW_ + 4, CH_ + 4, RGB(170, 176, 190));
    A->blit(c, ox, oy, &(nx_canvas){ cv, CW_, CH_, CW_, 0, 0, CW_, CH_ }, 0, 0, CW_, CH_);
    if (msg[0]) A->text(c, 12, c->h - 22, msg, NF_UI, RGB(90, 90, 100));
}
static void on_paint(nx_win *w) { draw(w); }
static void on_timer(nx_win *w) { if (msg[0] && ++msg_t > 300) { msg[0] = 0; A->win_invalidate(w); } }
static void on_mouse(nx_win *w, int type, int mx, int my, int btn, int arg) {
    (void)arg;
    int ox = (w->cv.w - CW_) / 2, oy = TOP + 10;
    if (type == NMOUSE_DOWN && my < TOP) {
        for (int t = 0; t < T_COUNT; t++) if (nx_inside(10 + t * 92, 8, 86, 26, mx, my)) { tool = t; A->win_invalidate(w); return; }
        for (int i = 0; i < 3; i++) if (nx_inside(10 + i * 40, 38, 34, 18, mx, my)) { size_i = i; A->win_invalidate(w); return; }
        for (int i = 0; i < 12; i++) if (nx_inside(10 + 3 * 40 + 20 + i * 24, 36, 20, 20, mx, my)) { color_i = i; A->win_invalidate(w); return; }
        int bx = w->cv.w - 210;
        if (nx_inside(bx, 8, 90, 26, mx, my)) { for (int i = 0; i < CW_ * CH_; i++) cv[i] = RGB(255, 255, 255); memcpy(snap, cv, CW_ * CH_ * 4); A->win_invalidate(w); return; }
        if (nx_inside(bx + 100, 8, 100, 26, mx, my)) { save(); A->win_invalidate(w); return; }
        return;
    }
    if (type == NMOUSE_MOVE) {
        w->cursor = (mx >= ox && my >= oy && mx < ox + CW_ && my < oy + CH_) ? NC_CROSS : NC_ARROW;
        if (!drawing) return;
    }
    int px = mx - ox, py = my - oy;
    if (type == NMOUSE_DOWN) {
        if (px < 0 || py < 0 || px >= CW_ || py >= CH_) return;
        uint32_t col = (btn & NB_RIGHT) ? RGB(255, 255, 255) : cur();
        if (tool == T_FILL) { fill_at(px, py, col); memcpy(snap, cv, CW_ * CH_ * 4); A->win_invalidate(w); return; }
        drawing = 1; sx = lx = px; sy = ly = py;
        memcpy(snap, cv, CW_ * CH_ * 4);
        if (tool == T_PEN || tool == T_BRUSH || tool == T_ERASER) {
            uint32_t c2 = tool == T_ERASER ? RGB(255, 255, 255) : col;
            put(px, py, c2, tool == T_BRUSH ? 6 : sizes[size_i]);
        }
        A->win_invalidate(w);
        return;
    }
    if (type == NMOUSE_MOVE && drawing) {
        int cx = px < 0 ? 0 : px >= CW_ ? CW_ - 1 : px, cy = py < 0 ? 0 : py >= CH_ ? CH_ - 1 : py;
        if (tool == T_PEN || tool == T_BRUSH || tool == T_ERASER) {
            uint32_t c2 = tool == T_ERASER ? RGB(255, 255, 255) : cur();
            line_px(lx, ly, cx, cy, c2, tool == T_BRUSH ? 6 : sizes[size_i]);
            lx = cx; ly = cy;
        } else preview(cx, cy);
        A->win_invalidate(w);
        return;
    }
    if (type == NMOUSE_UP && drawing) {
        drawing = 0;
        int cx = px < 0 ? 0 : px >= CW_ ? CW_ - 1 : px, cy = py < 0 ? 0 : py >= CH_ ? CH_ - 1 : py;
        if (tool == T_LINE || tool == T_RECT || tool == T_ELLIPSE) preview(cx, cy);
        memcpy(snap, cv, CW_ * CH_ * 4);
        A->win_invalidate(w);
    }
}
static void on_key(nx_win *w, int key, int mods) {
    if ((mods & NM_CTRL) && (key == 's' || key == 'S')) save();
    if ((mods & NM_CTRL) && (key == 'z' || key == 'Z')) { memcpy(cv, snap, CW_ * CH_ * 4); }
    A->win_invalidate(w);
}
static void on_close_dummy(void) {}
int app_main(const nx_api *api, const char *arg) {
    (void)api;
    cv = A->malloc(CW_ * CH_ * 4); snap = A->malloc(CW_ * CH_ * 4);
    if (!cv || !snap) return -1;
    for (int i = 0; i < CW_ * CH_; i++) cv[i] = RGB(255, 255, 255);
    memcpy(snap, cv, CW_ * CH_ * 4);
    msg[0] = 0; tool = T_PEN;
    win = A->win_create("Paint", CW_ + 16, CH_ + TOP + 24, 0);
    if (!win) return -2;
    win->on_paint = on_paint; win->on_mouse = on_mouse; win->on_key = on_key; win->on_timer = on_timer;
    A->win_timer(win, 100);
    if (arg && arg[0]) load(arg);
    (void)on_close_dummy;
    return 0;
}
