/* NegxusOS - carregador de executaveis .nxe e tabela de API (nx_api) */
#include "wm.h"

typedef struct {
    int id, used, wins, flags;
    u8 *base; u32 mem_size;
    u32 entry;
    char path[96];
} instance_t;

#define MAXINST 48
static instance_t inst[MAXINST];
static int next_id = 1;
static app_info_t apps[32];
static int napps;
static char clip[4096]; static int cliplen;
static nx_api api;
static int inited;

/* funcoes de janela (wm.c) */
void nx_win_title(nx_win *, const char *);
void nx_win_invalidate_rect(nx_win *, int, int, int, int);
void nx_win_invalidate(nx_win *);
void nx_win_close(nx_win *);
void nx_win_timer(nx_win *, int);
void nx_win_minsize(nx_win *, int, int);
void nx_win_set_size(nx_win *, int, int);


static instance_t *inst_by_id(int id) {
    for (int i = 0; i < MAXINST; i++) if (inst[i].used && inst[i].id == id) return &inst[i];
    return 0;
}
const u32 *nxe_instance_icon(int id) {
    instance_t *in = inst_by_id(id);
    if (!in) return 0;
    for (int i = 0; i < napps; i++) if (!strcmp(apps[i].path, in->path)) return apps[i].icon;
    return 0;
}
int nxe_current_app(void) { return g_alloc_tag; }
void nxe_set_current(int id) { g_alloc_tag = id; }

/* ---------------- leitura do cabecalho ---------------- */
static int read_header(const char *path, nxe_header *h, u32 *icon_out) {
    int sz = 0;
    u8 *buf = fs_load(path, &sz);
    if (!buf) return -1;
    if (sz < NXE_HDR_SIZE + NXE_ICON_PX * NXE_ICON_PX * 4 || memcmp(buf, "NXE1", 4)) { kfree(buf); return -1; }
    memcpy(h, buf, NXE_HDR_SIZE);
    if (h->magic != NXE_MAGIC) { kfree(buf); return -1; }
    if (icon_out) memcpy(icon_out, buf + NXE_HDR_SIZE, NXE_ICON_PX * NXE_ICON_PX * 4);
    kfree(buf);
    return 0;
}

void nxe_build_api(void);
void wm_paint_app(int app);
void nxe_init(void) {
    nxe_build_api();
    nx_dirent ents[48];
    napps = 0;
    int n = fs_list("/Apps", ents, 48);
    for (int i = 0; i < n && napps < 32; i++) {
        if (ents[i].is_dir) continue;
        int L = (int)strlen(ents[i].name);
        if (L < 5 || strcasecmp(ents[i].name + L - 4, ".nxe")) continue;
        nxe_header h;
        app_info_t *a = &apps[napps];
        nx_snprintf(a->path, sizeof a->path, "/Apps/%s", ents[i].name);
        if (read_header(a->path, &h, a->icon) < 0) continue;
        strncpy(a->name, h.name, 31); a->name[31] = 0;
        a->flags = h.flags; a->order = h.order;
        napps++;
    }
    kprintf("[nxe] %d aplicativos\n", napps);
}

int nxe_app_count(void) { return napps; }
const app_info_t *nxe_app(int i) { return (i >= 0 && i < napps) ? &apps[i] : 0; }
int nxe_icon(const char *path, u32 *out48) { nxe_header h; return read_header(path, &h, out48); }

/* ---------------- execucao ---------------- */
static instance_t *inst_alloc(void) {
    for (int i = 0; i < MAXINST; i++) if (!inst[i].used) {
        memset(&inst[i], 0, sizeof inst[i]);
        inst[i].used = 1; inst[i].id = next_id++;
        return &inst[i];
    }
    return 0;
}
static void inst_free(instance_t *in) {
    mem_free_tag(in->id);
    in->used = 0;
}

int nxe_exec(const char *path, const char *arg) {
    nxe_header h;
    int sz = 0;
    u8 *file = fs_load(path, &sz);
    if (!file) return -1;
    if (sz < NXE_HDR_SIZE + NXE_ICON_PX * NXE_ICON_PX * 4 || memcmp(file, "NXE1", 4)) { kfree(file); return -2; }
    memcpy(&h, file, NXE_HDR_SIZE);
    if (h.version != NX_API_VERSION) { kfree(file); return -3; }
    instance_t *in = inst_alloc();
    if (!in) { kfree(file); return -4; }
    strncpy(in->path, path, 95);
    in->mem_size = h.mem_size;
    if (h.image_off + h.image_size > (u32)sz || h.mem_size < h.image_size || h.reloc_off + h.reloc_count * 4 > (u32)sz) {
        in->used = 0; kfree(file); return -5;
    }
    int old = g_alloc_tag; g_alloc_tag = in->id;
    u8 *base = kzalloc(h.mem_size + 64);
    g_alloc_tag = old;
    if (!base) { in->used = 0; kfree(file); return -6; }
    in->base = base;
    memcpy(base, file + h.image_off, h.image_size);
    const u32 *rel = (const u32 *)(file + h.reloc_off);
    for (u32 i = 0; i < h.reloc_count; i++) {
        u32 off = rel[i];
        if (off + 8 > h.mem_size) continue;
        *(u64 *)(base + off) += (u64)(uintptr_t)base;
    }
    kfree(file);
    in->entry = h.entry;
    /* chama o ponto de entrada com a tag do aplicativo ativa */
    nxe_main_fn fn = (nxe_main_fn)(base + h.entry);
    g_alloc_tag = in->id;
    int r = fn(&api, arg ? arg : "");
    wm_paint_app(in->id);                  /* primeira pintura antes de o quadro ser composto */
    g_alloc_tag = 0;
    if (in->wins == 0) { if (r < 0) kprintf("[nxe] %s erro %d\n", path, r); inst_free(in); }
    return in->id;
}

void nxe_win_closed(int app) {
    instance_t *in = inst_by_id(app);
    if (!in) return;
    if (in->wins > 0) in->wins--;
    if (in->wins == 0) inst_free(in);
}

int nxe_open(const char *path) {
    int L = (int)strlen(path);
    if (L > 4 && !strcasecmp(path + L - 4, ".nxe")) return nxe_exec(path, 0) > 0 ? 0 : -1;
    if (L > 4 && !strcasecmp(path + L - 4, ".npt")) {        /* imagem do Paint */
        char a[96]; strncpy(a, path, 95); a[95] = 0;
        return nxe_exec("/Apps/paint.nxe", a) > 0 ? 0 : -1;
    }
    return -2;
}

const nx_api *nxe_api(void) { return &api; }
void clip_set(const char *s, int n) { if (n > (int)sizeof clip) n = sizeof clip; memcpy(clip, s, n); cliplen = n; }
int clip_get(char *buf, int max) { int n = MIN(cliplen, max); memcpy(buf, clip, n); return n; }

/* ================= wrappers da API ================= */
static void *a_malloc(size_t n) { return kmalloc(n); }
static void a_free(void *p) { kfree(p); }
static void *a_realloc(void *p, size_t n) { return krealloc(p, n); }
static nx_win *a_win_create(const char *t, int cw, int ch, int fl) {
    int id = g_alloc_tag;
    nx_win *w = wm_win_create(t, cw, ch, fl);
    if (w) { instance_t *in = inst_by_id(id); if (in) in->wins++; }
    return w;
}
static void a_ticks_dummy(void) {}
static uint32_t a_ticks(void) { return g_ticks; }
static void a_time(nx_time *t) { rtc_read(t); }
static uint32_t a_rand(void) { return rand32(); }
static void a_sysinfo(nx_sysinfo *si) { memset(si, 0, sizeof *si); si->ram_mb = mem_total_mb(); si->sw = g_sw; si->sh = g_sh; si->uptime_s = g_ticks / 1000; strcpy(si->cpu, "x86-64"); }
static void a_power(int reboot) { if (reboot) system_reboot(); else system_poweroff(); }
static void a_log(const char *s) { kputs(s); kputs("\n"); }
static int a_app_icon(const char *p, uint32_t *o) { return nxe_icon(p, o); }
static int a_exec(const char *p, const char *a) { int r = nxe_exec(p, a); return r > 0 ? 0 : r; }
static int a_open(const char *p) { return nxe_open(p); }
static void a_clip_set(const char *s, int n) { clip_set(s, n); }
static int a_clip_get(char *b, int m) { return clip_get(b, m); }
void desk_set_wallpaper(int id);
static void a_set_wallpaper(int id) { desk_set_wallpaper(id); }
static void a_fill(nx_canvas *c, int x, int y, int w, int h, uint32_t col) { cv_fill(c, x, y, w, h, col); }
static void a_fill_a(nx_canvas *c, int x, int y, int w, int h, uint32_t col, int a) { cv_fill_a(c, x, y, w, h, col, a); }
static void a_rect(nx_canvas *c, int x, int y, int w, int h, uint32_t col) { cv_rect(c, x, y, w, h, col); }
static void a_rrect(nx_canvas *c, int x, int y, int w, int h, int r, uint32_t col) { cv_rrect(c, x, y, w, h, r, col); }
static void a_rrect_line(nx_canvas *c, int x, int y, int w, int h, int r, uint32_t col) { cv_rrect_line(c, x, y, w, h, r, col); }
static void a_line(nx_canvas *c, int x0, int y0, int x1, int y1, uint32_t col) { cv_line(c, x0, y0, x1, y1, col); }
static void a_circle(nx_canvas *c, int cx, int cy, int r, uint32_t col) { cv_circle(c, cx, cy, r, col); }
static void a_text(nx_canvas *c, int x, int y, const char *s, int f, uint32_t col) { cv_text(c, x, y, s, f, col); }
static int a_text_w(const char *s, int f) { return text_width(s, f); }
static int a_text_h(int f) { return text_height(f); }
static void a_blit(nx_canvas *d, int x, int y, const nx_canvas *s, int sx, int sy, int w, int h) { cv_blit(d, x, y, s, sx, sy, w, h); }
static void a_icon(nx_canvas *c, const uint32_t *ic, int x, int y, int s) { cv_icon(c, ic, x, y, s); }
static void a_win_inval(nx_win *w) { nx_win_invalidate(w); }
static void a_win_inval_rect(nx_win *w, int x, int y, int ww, int hh) { nx_win_invalidate_rect(w, x, y, ww, hh); }
static void a_win_title(nx_win *w, const char *t) { nx_win_title(w, t); }
static void a_win_close(nx_win *w) { nx_win_close(w); }
static void a_win_timer(nx_win *w, int ms) { nx_win_timer(w, ms); }
static void a_win_size(nx_win *w, int cw, int ch) { nx_win_set_size(w, cw, ch); }
static void a_win_minsize(nx_win *w, int cw, int ch) { nx_win_minsize(w, cw, ch); }
static int a_fs_read(const char *p, void *b, int m) { return fs_read(p, b, m); }
static int a_fs_write(const char *p, const void *b, int n) { return fs_write(p, b, n); }
static int a_fs_list(const char *p, nx_dirent *o, int m) { return fs_list(p, o, m); }
static int a_fs_stat(const char *p, nx_stat *s) { return fs_stat(p, s); }
static int a_fs_mkdir(const char *p) { return fs_mkdir(p); }
static int a_fs_remove(const char *p) { return fs_remove(p); }
static int a_fs_rename(const char *f, const char *t) { return fs_rename(f, t); }

void nxe_build_api(void) {
    memset(&api, 0, sizeof api);
    api.version = NX_API_VERSION;
    api.malloc = a_malloc; api.free = a_free; api.realloc = a_realloc;
    api.win_create = a_win_create; api.win_title = a_win_title; api.win_invalidate = a_win_inval;
    api.win_invalidate_rect = a_win_inval_rect; api.win_close = a_win_close; api.win_timer = a_win_timer;
    api.win_size = a_win_size; api.win_minsize = a_win_minsize;
    api.fill = a_fill; api.fill_a = a_fill_a; api.rect = a_rect; api.rrect = a_rrect; api.rrect_line = a_rrect_line;
    api.line = a_line; api.circle = a_circle; api.text = a_text; api.text_w = a_text_w; api.text_h = a_text_h;
    api.blit = a_blit; api.icon = a_icon;
    api.fs_read = a_fs_read; api.fs_write = a_fs_write; api.fs_list = a_fs_list; api.fs_stat = a_fs_stat;
    api.fs_mkdir = a_fs_mkdir; api.fs_remove = a_fs_remove; api.fs_rename = a_fs_rename;
    api.ticks = a_ticks; api.time = a_time; api.rand = a_rand; api.exec = a_exec; api.open = a_open;
    api.clip_set = a_clip_set; api.clip_get = a_clip_get; api.sysinfo = a_sysinfo; api.power = a_power;
    api.log = a_log; api.app_icon = a_app_icon; api.set_wallpaper = a_set_wallpaper;
    (void)a_ticks_dummy;
}
