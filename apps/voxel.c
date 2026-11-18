/* Cubos: clone de voxel tipo Minecraft com renderizador por software (inteiro, sem FPU).
   Tabelas de seno/cosseno em ponto fixo 14 bits (gerado). */
#include "../sdk/apputil.h"
#define WX 48
#define WZ 48
#define WY 20
#define RW 128
#define RH 80
#define SCALE 4
#define ONE 65536
#define MAXT (40 * ONE)
enum { AIR = 0, GRASS, DIRT, STONE, WOOD, LEAF, BEDROCK, NBLK };
static const int COS_T[256] = {
  16384,16379,16364,16340,16305,16261,16207,16143,16069,15986,15893,15791,15679,15557,15426,15286,
  15137,14978,14811,14635,14449,14256,14053,13842,13623,13395,13160,12916,12665,12406,12140,11866,
  11585,11297,11003,10702,10394,10080,9760,9434,9102,8765,8423,8076,7723,7366,7005,6639,
  6270,5897,5520,5139,4756,4370,3981,3590,3196,2801,2404,2006,1606,1205,804,402,
  0,-402,-804,-1205,-1606,-2006,-2404,-2801,-3196,-3590,-3981,-4370,-4756,-5139,-5520,-5897,
  -6270,-6639,-7005,-7366,-7723,-8076,-8423,-8765,-9102,-9434,-9760,-10080,-10394,-10702,-11003,-11297,
  -11585,-11866,-12140,-12406,-12665,-12916,-13160,-13395,-13623,-13842,-14053,-14256,-14449,-14635,-14811,-14978,
  -15137,-15286,-15426,-15557,-15679,-15791,-15893,-15986,-16069,-16143,-16207,-16261,-16305,-16340,-16364,-16379,
  -16384,-16379,-16364,-16340,-16305,-16261,-16207,-16143,-16069,-15986,-15893,-15791,-15679,-15557,-15426,-15286,
  -15137,-14978,-14811,-14635,-14449,-14256,-14053,-13842,-13623,-13395,-13160,-12916,-12665,-12406,-12140,-11866,
  -11585,-11297,-11003,-10702,-10394,-10080,-9760,-9434,-9102,-8765,-8423,-8076,-7723,-7366,-7005,-6639,
  -6270,-5897,-5520,-5139,-4756,-4370,-3981,-3590,-3196,-2801,-2404,-2006,-1606,-1205,-804,-402,
  0,402,804,1205,1606,2006,2404,2801,3196,3590,3981,4370,4756,5139,5520,5897,
  6270,6639,7005,7366,7723,8076,8423,8765,9102,9434,9760,10080,10394,10702,11003,11297,
  11585,11866,12140,12406,12665,12916,13160,13395,13623,13842,14053,14256,14449,14635,14811,14978,
  15137,15286,15426,15557,15679,15791,15893,15986,16069,16143,16207,16261,16305,16340,16364,16379,
};
static const int SIN_T[256] = {
  0,402,804,1205,1606,2006,2404,2801,3196,3590,3981,4370,4756,5139,5520,5897,
  6270,6639,7005,7366,7723,8076,8423,8765,9102,9434,9760,10080,10394,10702,11003,11297,
  11585,11866,12140,12406,12665,12916,13160,13395,13623,13842,14053,14256,14449,14635,14811,14978,
  15137,15286,15426,15557,15679,15791,15893,15986,16069,16143,16207,16261,16305,16340,16364,16379,
  16384,16379,16364,16340,16305,16261,16207,16143,16069,15986,15893,15791,15679,15557,15426,15286,
  15137,14978,14811,14635,14449,14256,14053,13842,13623,13395,13160,12916,12665,12406,12140,11866,
  11585,11297,11003,10702,10394,10080,9760,9434,9102,8765,8423,8076,7723,7366,7005,6639,
  6270,5897,5520,5139,4756,4370,3981,3590,3196,2801,2404,2006,1606,1205,804,402,
  0,-402,-804,-1205,-1606,-2006,-2404,-2801,-3196,-3590,-3981,-4370,-4756,-5139,-5520,-5897,
  -6270,-6639,-7005,-7366,-7723,-8076,-8423,-8765,-9102,-9434,-9760,-10080,-10394,-10702,-11003,-11297,
  -11585,-11866,-12140,-12406,-12665,-12916,-13160,-13395,-13623,-13842,-14053,-14256,-14449,-14635,-14811,-14978,
  -15137,-15286,-15426,-15557,-15679,-15791,-15893,-15986,-16069,-16143,-16207,-16261,-16305,-16340,-16364,-16379,
  -16384,-16379,-16364,-16340,-16305,-16261,-16207,-16143,-16069,-15986,-15893,-15791,-15679,-15557,-15426,-15286,
  -15137,-14978,-14811,-14635,-14449,-14256,-14053,-13842,-13623,-13395,-13160,-12916,-12665,-12406,-12140,-11866,
  -11585,-11297,-11003,-10702,-10394,-10080,-9760,-9434,-9102,-8765,-8423,-8076,-7723,-7366,-7005,-6639,
  -6270,-5897,-5520,-5139,-4756,-4370,-3981,-3590,-3196,-2801,-2404,-2006,-1606,-1205,-804,-402,
};

static nx_win *win;
static unsigned char world[WX * WZ * WY];
static uint32_t lr[RW * RH];
static int px_, py_, pz_, yaw = 40, sel = GRASS;
static int hit_flag, hit_x, hit_y, hit_z, pre_x, pre_y, pre_z, hit_face, hit_t;
static const char *blk_name[NBLK] = { "ar", "grama", "terra", "pedra", "madeira", "folha", "bedrock" };

static int blk(int x, int y, int z) {
    if (x < 0 || z < 0 || y < 0 || x >= WX || z >= WZ || y >= WY) return AIR;
    return world[(y * WZ + z) * WX + x];
}
static void setb(int x, int y, int z, int t) {
    if (x < 0 || z < 0 || y < 0 || x >= WX || z >= WZ || y >= WY) return;
    world[(y * WZ + z) * WX + x] = (unsigned char)t;
}
static int hash2(int x, int z) {
    unsigned h = (unsigned)(x * 374761393 + z * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h ^ (h >> 16)) & 0xFF);
}
static void gen_world(void) {
    for (int z = 0; z < WZ; z++) for (int x = 0; x < WX; x++) {
        int h = 7 + (COS_T[(x * 9) & 255] * 3 + COS_T[(z * 11 + 70) & 255] * 3) / 16384 + 3;
        if (h < 3) h = 3; if (h > WY - 6) h = WY - 6;
        for (int y = 0; y < WY; y++) {
            int t = AIR;
            if (y == 0) t = BEDROCK;
            else if (y < h - 3) t = STONE;
            else if (y < h - 1) t = DIRT;
            else if (y == h - 1) t = GRASS;
            setb(x, y, z, t);
        }
    }
    for (int z = 3; z < WZ - 4; z += 6) for (int x = 3; x < WX - 4; x += 7) {
        if (hash2(x, z) & 1) continue;
        int base = 0;
        for (int y = 1; y < WY; y++) if (blk(x, y, z) == GRASS) base = y + 1;
        if (!base) continue;
        for (int k = 0; k < 3 && base + k < WY; k++) setb(x, base + k, z, WOOD);
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++)
            for (int dy = 2; dy <= 3; dy++) {
                int yy = base + dy;
                if (yy < WY && blk(x + dx, yy, z + dz) == AIR) setb(x + dx, yy, z + dz, LEAF);
            }
    }
}

/* DDA de voxel (Amanatides-Woo) em ponto fixo 16.16 */
static int cast(int ox, int oy, int oz, int dx, int dy, int dz, int maxt) {
    int o[3] = { ox, oy, oz }, d[3] = { dx, dy, dz };
    int c[3], st[3], tmax[3], tdel[3];
    for (int a = 0; a < 3; a++) {
        c[a] = o[a] >> 16;
        if (d[a] == 0) { st[a] = 0; tmax[a] = 0x7FFFFFFF; tdel[a] = 0x7FFFFFFF; continue; }
        st[a] = d[a] > 0 ? 1 : -1;
        int ad = d[a] > 0 ? d[a] : -d[a];
        long long td = ((long long)1 << 32) / ad;
        tdel[a] = td > 0x7FFFFFFF ? 0x7FFFFFFF : (int)td;
        long long dist = st[a] > 0 ? ((long long)(c[a] + 1) << 16) - o[a] : (long long)o[a] - ((long long)c[a] << 16);
        long long tm = dist * td >> 16;
        tmax[a] = tm > 0x7FFFFFFF ? 0x7FFFFFFF : (int)tm;
    }
    int pc[3] = { c[0], c[1], c[2] }, face = 0;
    hit_flag = 0;
    for (int guard = 0; guard < 200; guard++) {
        int a = 0;
        if (tmax[1] < tmax[a]) a = 1;
        if (tmax[2] < tmax[a]) a = 2;
        int t = tmax[a];
        if (t > maxt) break;
        pc[0] = c[0]; pc[1] = c[1]; pc[2] = c[2];
        c[a] += st[a];
        tmax[a] += tdel[a];
        face = a * 2 + (st[a] > 0 ? 1 : 0);
        if (c[1] < 0 || c[1] >= WY || c[0] < 0 || c[2] < 0 || c[0] >= WX || c[2] >= WZ) break;
        int b = blk(c[0], c[1], c[2]);
        if (b != AIR) {
            hit_flag = 1; hit_x = c[0]; hit_y = c[1]; hit_z = c[2];
            pre_x = pc[0]; pre_y = pc[1]; pre_z = pc[2];
            hit_face = face; hit_t = t;
            return 1;
        }
    }
    return 0;
}

static uint32_t shade(int b, int face, int t) {
    static const uint32_t top[NBLK] = { 0, RGB(92,178,70), RGB(134,96,67), RGB(130,130,130), RGB(120,88,52), RGB(44,140,44), RGB(60,60,60) };
    static const uint32_t side[NBLK] = { 0, RGB(122,92,58), RGB(134,96,67), RGB(130,130,130), RGB(108,78,44), RGB(44,140,44), RGB(60,60,60) };
    uint32_t col = top[b];
    int ax = face / 2, pos = face & 1;
    if (ax == 1) col = pos ? top[b] : side[b];          /* topo ou base */
    else col = side[b];
    int k = (ax == 1) ? (pos ? 255 : 150) : (ax == 0 ? 190 : 165);
    uint32_t r = (col >> 16) & 255, g = (col >> 8) & 255, bb = col & 255;
    r = r * k / 255; g = g * k / 255; bb = bb * k / 255;
    int fog = t * 255 / MAXT; if (fog > 255) fog = 255;
    uint32_t sky = RGB(150, 200, 255);
    r = (r * (255 - fog) + ((sky >> 16) & 255) * fog) / 255;
    g = (g * (255 - fog) + ((sky >> 8) & 255) * fog) / 255;
    bb = (bb * (255 - fog) + (sky & 255) * fog) / 255;
    return RGB(r, g, bb);
}

/* ===== 3D real: rasterizacao de triangulos com z-buffer e projecao em perspectiva ===== */
#define NEAR 13107                 /* 0.2 bloco (16.16): abaixo disso o triangulo e descartado */
#define FOC  (RW * 3 / 2)          /* distancia focal em pixels (campo de visao ~ 67 graus) */
#define VIEW_R 14                  /* raio de blocos desenhados ao redor da camera */
typedef struct { int x, y, iz; } sv_t;   /* vertice na tela: pixel e 1/z (16.16) */
static int zbuf[RW * RH];                /* 1/z: maior = mais perto */

static int project(long long wx, long long wy, long long wz, sv_t *o) {
    long long dx = wx - px_, dy = wy - py_, dz = wz - pz_;
    long long zc = (dx * SIN_T[yaw & 255] + dz * COS_T[yaw & 255]) / 16384;
    long long xc = (dx * COS_T[yaw & 255] - dz * SIN_T[yaw & 255]) / 16384;
    if (zc < NEAR) return 0;
    o->x = RW / 2 + (int)(xc * FOC / zc);
    o->y = RH / 2 - (int)(dy * FOC / zc);
    o->iz = (int)(((long long)1 << 32) / zc);
    return 1;
}

static void raster(sv_t a, sv_t b, sv_t c, uint32_t col) {
    long long area = (long long)(b.x - a.x) * (c.y - a.y) - (long long)(b.y - a.y) * (c.x - a.x);
    if (area == 0) return;
    int minx = nx_imin(a.x, nx_imin(b.x, c.x)), maxx = nx_imax(a.x, nx_imax(b.x, c.x));
    int miny = nx_imin(a.y, nx_imin(b.y, c.y)), maxy = nx_imax(a.y, nx_imax(b.y, c.y));
    if (minx < 0) minx = 0; if (miny < 0) miny = 0;
    if (maxx > RW - 1) maxx = RW - 1; if (maxy > RH - 1) maxy = RH - 1;
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
        long long w0 = (long long)(b.x - x) * (c.y - y) - (long long)(b.y - y) * (c.x - x);
        long long w1 = (long long)(c.x - x) * (a.y - y) - (long long)(c.y - y) * (a.x - x);
        long long w2 = (long long)(a.x - x) * (b.y - y) - (long long)(a.y - y) * (b.x - x);
        if (area > 0 ? (w0 < 0 || w1 < 0 || w2 < 0) : (w0 > 0 || w1 > 0 || w2 > 0)) continue;
        long long iz = (w0 * a.iz + w1 * b.iz + w2 * c.iz) / area;
        int idx = y * RW + x;
        if (iz > zbuf[idx]) { zbuf[idx] = (int)iz; lr[idx] = col; }
    }
}

/* desenha a face de um bloco. axis: 0 x,1 y,2 z ; sg: +1 ou -1 ; (bx,by,bz) bloco ; col cor */
static void quad_face(int bx, int by, int bz, int axis, int sg, uint32_t col) {
    long long base[3] = { (long long)bx << 16, (long long)by << 16, (long long)bz << 16 };
    long long P[4][3];
    int u = (axis + 1) % 3, v = (axis + 2) % 3;
    long long plane = base[axis] + (sg > 0 ? ONE : 0);
    /* cantos do quadrado no sentido anti-horario */
    static const int cu[4] = { 0, 1, 1, 0 }, cv[4] = { 0, 0, 1, 1 };
    for (int k = 0; k < 4; k++) {
        P[k][axis] = plane;
        P[k][u] = base[u] + cu[k] * ONE;
        P[k][v] = base[v] + cv[k] * ONE;
    }
    sv_t q[4];
    for (int k = 0; k < 4; k++) if (!project(P[k][0], P[k][1], P[k][2], &q[k])) return;
    raster(q[0], q[1], q[2], col);
    raster(q[0], q[2], q[3], col);
}

static void render(void) {
    /* ceu */
    for (int j = 0; j < RH; j++) {
        uint32_t sky = RGB(120 + j, 180 + j / 2, 255);
        for (int i = 0; i < RW; i++) lr[j * RW + i] = sky;
    }
    memset(zbuf, 0, sizeof zbuf);
    int cbx = (int)(px_ >> 16), cbz = (int)(pz_ >> 16);
    for (int y = 0; y < WY; y++)
    for (int z = cbz - VIEW_R; z <= cbz + VIEW_R; z++)
    for (int x = cbx - VIEW_R; x <= cbx + VIEW_R; x++) {
        int b = blk(x, y, z);
        if (b == AIR) continue;
        /* faces visiveis: vizinho vazio e camera do lado de fora */
        static const int nb[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        for (int f = 0; f < 6; f++) {
            int ax = f / 2, sg = (f & 1) ? -1 : 1;
            if (blk(x + nb[f][0], y + nb[f][1], z + nb[f][2]) != AIR) continue;
            long long cam[3] = { px_, py_, pz_ };
            long long bc[3] = { (long long)x << 16, (long long)y << 16, (long long)z << 16 };
            long long plane = bc[ax] + (sg > 0 ? ONE : 0);
            if ((cam[ax] - plane) * sg <= 0) continue;   /* face de tras */
            uint32_t col;
            if (ax == 1) col = sg > 0 ? shade(b, 2 * 1 + 1, 0) : shade(b, 2 * 1 + 0, 0);
            else col = shade(b, f, 0);
            /* sombreamento direcional + nevoa pela distancia do bloco */
            int dxb = x - cbx, dzb = z - cbz;
            int dist = dxb * dxb + dzb * dzb;
            (void)dist;
            quad_face(x, y, z, ax, sg, col);
        }
    }
}

static void on_paint(nx_win *w) {
    nx_canvas *c = &w->cv;
    for (int y = 0; y < c->h; y++) {
        int ly = y / SCALE; if (ly >= RH) ly = RH - 1;
        uint32_t *dst = c->px + y * c->pitch;
        for (int x = 0; x < c->w; x++) {
            int lx = x / SCALE; if (lx >= RW) lx = RW - 1;
            dst[x] = lr[ly * RW + lx];
        }
    }
    /* mira */
    int cx = c->w / 2, cy = c->h / 2;
    A->fill(c, cx - 8, cy - 1, 17, 3, RGB(255, 255, 255));
    A->fill(c, cx - 1, cy - 8, 3, 17, RGB(255, 255, 255));
    char hud[80];
    nx_snprintf(hud, sizeof hud, "Bloco: %s  [1-4]  clique esq.: quebra  dir.: coloca", blk_name[sel]);
    A->fill_a(c, 4, c->h - 22, c->w - 8, 18, RGB(0, 0, 0), 150);
    A->text(c, 10, c->h - 20, hud, NF_SM, RGB(255, 255, 255));
}

static void on_timer(nx_win *w) {
    render();
    A->win_invalidate(w);
}

static void on_key(nx_win *w, int key, int mods) {
    (void)mods;
    int step = 6 * 65536 / 16;                                   /* passo: 0.375 blocos */
    int sx = (int)((long long)step * SIN_T[yaw & 255] / 16384);
    int sz = (int)((long long)step * COS_T[yaw & 255] / 16384);
    int rxs = sz, rzs = -sx;                                     /* vetor direita */
    if (key == 'w' || key == 'W') { px_ += sx; pz_ += sz; }
    else if (key == 's' || key == 'S') { px_ -= sx; pz_ -= sz; }
    else if (key == 'a' || key == 'A') { px_ -= rxs; pz_ -= rzs; }
    else if (key == 'd' || key == 'D') { px_ += rxs; pz_ += rzs; }
    else if (key == ' ') py_ += step;
    else if (key == 'x' || key == 'X') py_ -= step;
    else if (key == NK_LEFT) yaw = (yaw - 8) & 255;
    else if (key == NK_RIGHT) yaw = (yaw + 8) & 255;
    else if (key >= '1' && key <= '4') sel = (key - '0') == 1 ? GRASS : (key - '0') == 2 ? DIRT : (key - '0') == 3 ? STONE : WOOD;
    else return;
    if (py_ < 4 * 65536) py_ = 4 * 65536;
    if (py_ > (WY - 1) * 65536) py_ = (WY - 1) * 65536;
    render();
    A->win_invalidate(w);
}

static void on_mouse(nx_win *w, int type, int x, int y, int btn, int arg) {
    (void)x; (void)y; (void)arg;
    if (type != NMOUSE_DOWN) return;
    int fx = SIN_T[yaw & 255] * 4, fz = COS_T[yaw & 255] * 4;
    int dy = 0;
    if (!cast(px_, py_, pz_, fx, dy, fz, 8 * ONE)) return;
    if (btn & NB_LEFT) setb(hit_x, hit_y, hit_z, AIR);
    else if (btn & NB_RIGHT) setb(pre_x, pre_y, pre_z, sel);
    render();
    A->win_invalidate(w);
}

int app_main(const nx_api *api, const char *arg) {
    (void)api; (void)arg;
    gen_world();
    px_ = (WX / 2) * ONE + ONE / 2;
    pz_ = (WZ / 2) * ONE + ONE / 2;
    py_ = 13 * ONE;
    win = A->win_create("Cubos (Minecraft clone)", RW * SCALE, RH * SCALE + 0, NW_FIXED);
    if (!win) return -1;
    win->on_paint = on_paint;
    win->on_key = on_key;
    win->on_mouse = on_mouse;
    win->on_timer = on_timer;
    render();
    A->win_timer(win, 60);
    A->win_invalidate(win);
    return 0;
}
