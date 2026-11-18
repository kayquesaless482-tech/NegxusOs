/* NegxusOS - kernel.h : interfaces internas do kernel */
#pragma once
#include "../sdk/nxe.h"

typedef uint8_t u8;  typedef int8_t s8;
typedef uint16_t u16; typedef int16_t s16;
typedef uint32_t u32; typedef int32_t s32;
typedef uint64_t u64; typedef int64_t s64;

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/* ---------------- portas / cpu ---------------- */
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { asm volatile("outw %0,%1" : : "a"(v), "Nd"(p)); }
static inline u16 inw(u16 p) { u16 v; asm volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(u16 p, u32 v) { asm volatile("outl %0,%1" : : "a"(v), "Nd"(p)); }
static inline u32 inl(u16 p) { u32 v; asm volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void cli(void) { asm volatile("cli" ::: "memory"); }
static inline void sti(void) { asm volatile("sti" ::: "memory"); }
static inline u64 rdtsc(void) { u32 a, d; asm volatile("rdtsc" : "=a"(a), "=d"(d)); return ((u64)d << 32) | a; }
static inline u64 read_flags(void) { u64 f; asm volatile("pushfq; pop %0" : "=r"(f)); return f; }

/* ---------------- boot ---------------- */
typedef struct { u64 base, len; u32 type, attr; } mmap_entry_t;
typedef struct {
    u32 magic; u32 fb; u16 w, h, pitch, bpp; u32 mmap_count; u8 drive, pad[3];
    mmap_entry_t mmap[48];
} __attribute__((packed)) bootinfo_t;
extern const bootinfo_t *g_boot;
extern char __initrd_start[], __initrd_end[];

/* ---------------- arch ---------------- */
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 int_no, err, rip, cs, rflags, rsp, ss;
} regs_t;
extern volatile u32 g_ticks;                 /* ms desde o boot */
void serial_init(void);
void kputs(const char *s);
void kprintf(const char *fmt, ...);
void arch_init(void);
void irq_register(int irq, void (*fn)(void));
void map_uncached(u64 phys, u64 len);
void cpu_idle(void);
void system_reboot(void);
void system_poweroff(void);
void panic_screen(const char *msg, regs_t *r);
void syscall_dispatch(regs_t *r);
void ring3_selftest(void);
void wm_paint_app(int app);
u32  rand32(void);
void rtc_read(nx_time *t);

/* ---------------- memoria ---------------- */
void mem_init(const bootinfo_t *bi);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void *krealloc(void *p, size_t n);
void kfree(void *p);
void mem_free_tag(int tag);
u32  mem_total_mb(void);
u32  mem_free_kb(void);
extern int g_alloc_tag;                      /* 0 = kernel */

/* ---------------- graficos ---------------- */
typedef struct { u8 adv, w, h; s8 bx, by; u32 off; } glyph_t;
typedef struct { int height, ascent; const glyph_t *g; const u8 *alpha; } font_t;
#define NFONTS NF_COUNT
extern const font_t font_table[NFONTS];
extern nx_canvas g_screen;                   /* back buffer */
void gfx_init(const bootinfo_t *bi);
void gfx_present(int x, int y, int w, int h);   /* copia do back buffer ao framebuffer */
nx_canvas cv_make(u32 *px, int w, int h, int pitch);
void cv_clip(nx_canvas *c, int x, int y, int w, int h);   /* define recorte (interseta com o canvas) */
static inline u32 blend(u32 d, u32 s, int a) {
    if (a <= 0) return d;
    if (a >= 255) return s & 0xFFFFFF;
    u32 ia = 255 - a;
    u32 rb = (s & 0xFF00FF) * a + (d & 0xFF00FF) * ia;
    u32 g = ((s >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia;
    rb = ((rb + 0x00800080 + ((rb >> 8) & 0x00FF00FF)) >> 8) & 0x00FF00FF;
    g = ((g + 128 + (g >> 8)) >> 8) & 0xFF;
    return rb | (g << 8);
}
void cv_fill(nx_canvas *c, int x, int y, int w, int h, u32 col);
void cv_fill_a(nx_canvas *c, int x, int y, int w, int h, u32 col, int a);
void cv_rect(nx_canvas *c, int x, int y, int w, int h, u32 col);
void cv_rrect(nx_canvas *c, int x, int y, int w, int h, int r, u32 col);
void cv_rrect_a(nx_canvas *c, int x, int y, int w, int h, int r, u32 col, int a);
void cv_rrect_line(nx_canvas *c, int x, int y, int w, int h, int r, u32 col);
void cv_line(nx_canvas *c, int x0, int y0, int x1, int y1, u32 col);
void cv_circle(nx_canvas *c, int cx, int cy, int r, u32 col);
void cv_text(nx_canvas *c, int x, int y, const char *s, int font, u32 col);
int  text_width(const char *s, int font);
int  text_height(int font);
void cv_blit(nx_canvas *d, int x, int y, const nx_canvas *s, int sx, int sy, int w, int h);
void cv_icon(nx_canvas *c, const u32 *icon48, int x, int y, int size);
void cv_gradient_v(nx_canvas *c, int x, int y, int w, int h, u32 top, u32 bot);
u32  rgb_mix(u32 a, u32 b, int t256);        /* mistura simples */
int  isqrt32(u32 v);

/* ---------------- entrada ---------------- */
enum { EV_KEY = 1, EV_MOUSE };
typedef struct { int type; int a, b, c, d; } event_t;   /* KEY: a=key b=mods ; MOUSE: a=dx b=dy c=buttons d=wheel */
void input_init(void);
int  input_next(event_t *e);
extern int g_kbd_dead;

/* ---------------- arquivos (ramfs) ---------------- */
void fs_init(void);
int  fs_read(const char *path, void *buf, int max);
int  fs_write(const char *path, const void *buf, int n);
int  fs_list(const char *path, nx_dirent *out, int max);
int  fs_stat(const char *path, nx_stat *st);
int  fs_mkdir(const char *path);
int  fs_remove(const char *path);
int  fs_rename(const char *from, const char *to);
void *fs_load(const char *path, int *size);          /* kmalloc'ado */

/* ---------------- janelas / desktop ---------------- */
void wm_init(void);
void wm_handle_event(const event_t *e);
void wm_tick(void);
int  wm_needs_redraw(void);
void wm_redraw(void);
void wm_splash(int percent, const char *msg);
nx_win *wm_win_create(const char *title, int cw, int ch, int flags);
void wm_close_app_windows(int app_id);
extern int g_wm_ready;

/* ---------------- executaveis ---------------- */
typedef struct app_info {
    char path[96]; char name[32]; u32 flags; u32 order; u32 icon[NXE_ICON_PX * NXE_ICON_PX];
} app_info_t;
void nxe_init(void);
int  nxe_exec(const char *path, const char *arg);
int  nxe_open(const char *path);
int  nxe_icon(const char *path, u32 *out48);
int  nxe_app_count(void);
const app_info_t *nxe_app(int i);
int  nxe_current_app(void);
void nxe_set_current(int id);
void nxe_win_closed(int app_id);
const nx_api *nxe_api(void);
void clip_set(const char *s, int n);
int  clip_get(char *buf, int max);

