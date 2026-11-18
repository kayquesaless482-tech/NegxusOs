/* NegxusOS - entrada: teclado PS/2 com layout ABNT2 (pt-BR) e mouse PS/2 com roda */
#include "kernel.h"

#define QSIZE 512
static event_t evq[QSIZE];
static volatile int qh, qt;
static void push(event_t e) {
    int n = (qt + 1) % QSIZE;
    if (n == qh) return;
    evq[qt] = e; qt = n;
}
int input_next(event_t *e) {
    if (qh == qt) return 0;
    *e = evq[qh]; qh = (qh + 1) % QSIZE;
    return 1;
}

/* ================= controlador PS/2 ================= */
static int wait_in(void) { for (int i = 0; i < 200000; i++) if (!(inb(0x64) & 2)) return 1; return 0; }
static int wait_out(void) { for (int i = 0; i < 200000; i++) if (inb(0x64) & 1) return 1; return 0; }
static void kbc_cmd(u8 c) { wait_in(); outb(0x64, c); }
static void kbc_data(u8 d) { wait_in(); outb(0x60, d); }
static int kbc_read(void) { return wait_out() ? inb(0x60) : -1; }
static int mouse_cmd(u8 d) { kbc_cmd(0xD4); kbc_data(d); return kbc_read(); }

/* ================= layout ABNT2 ================= */
enum { D_ACUTE = 1, D_GRAVE, D_TILDE, D_CIRC, D_DIAER };
typedef struct { u8 n, s, g; } keydef_t;       /* normal, shift, AltGr (codificacao CP1252) */
static const keydef_t abnt2[0x80] = {
    [0x29] = {'\'', '"', 0},    [0x02] = {'1', '!', 0xB9},  [0x03] = {'2', '@', 0xB2},
    [0x04] = {'3', '#', 0xB3},  [0x05] = {'4', '$', 0xA3},  [0x06] = {'5', '%', 0xA2},
    [0x07] = {'6', D_DIAER, 0xAC}, [0x08] = {'7', '&', 0},  [0x09] = {'8', '*', 0},
    [0x0A] = {'9', '(', 0},     [0x0B] = {'0', ')', 0},     [0x0C] = {'-', '_', 0},
    [0x0D] = {'=', '+', 0xA7},
    [0x10] = {'q', 'Q', '/'},   [0x11] = {'w', 'W', '?'},   [0x12] = {'e', 'E', 0x80},
    [0x13] = {'r', 'R', 0},     [0x14] = {'t', 'T', 0},     [0x15] = {'y', 'Y', 0},
    [0x16] = {'u', 'U', 0},     [0x17] = {'i', 'I', 0},     [0x18] = {'o', 'O', 0},
    [0x19] = {'p', 'P', 0},     [0x1A] = {D_ACUTE, D_GRAVE, 0}, [0x1B] = {'[', '{', 0xAA},
    [0x1E] = {'a', 'A', 0},     [0x1F] = {'s', 'S', 0},     [0x20] = {'d', 'D', 0},
    [0x21] = {'f', 'F', 0},     [0x22] = {'g', 'G', 0},     [0x23] = {'h', 'H', 0},
    [0x24] = {'j', 'J', 0},     [0x25] = {'k', 'K', 0},     [0x26] = {'l', 'L', 0},
    [0x27] = {0xE7, 0xC7, 0},   [0x28] = {D_TILDE, D_CIRC, 0}, [0x2B] = {']', '}', 0xBA},
    [0x2C] = {'z', 'Z', 0},     [0x2D] = {'x', 'X', 0},     [0x2E] = {'c', 'C', 0},
    [0x2F] = {'v', 'V', 0},     [0x30] = {'b', 'B', 0},     [0x31] = {'n', 'N', 0},
    [0x32] = {'m', 'M', 0},     [0x33] = {',', '<', 0},     [0x34] = {'.', '>', 0},
    [0x35] = {';', ':', 0},     [0x39] = {' ', ' ', ' '},   [0x56] = {'\\', '|', 0},
    [0x73] = {'/', '?', 0xB0},
};
static const u8 accent_char[6] = { 0, 0xB4, '`', '~', '^', 0xA8 };

typedef struct { const char *base; const u8 *res; } comp_t;
static const u8 r_acute[] = { 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xE7, 0xC7 };
static const u8 r_grave[] = { 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9 };
static const u8 r_tilde[] = { 0xE3, 0xF5, 0xF1, 0xC3, 0xD5, 0xD1 };
static const u8 r_circ[]  = { 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB };
static const u8 r_diaer[] = { 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xFF, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC, 0x9F };
static const comp_t comp[6] = {
    { 0, 0 },
    { "aeiouAEIOUcC", r_acute }, { "aeiouAEIOU", r_grave }, { "aonAON", r_tilde },
    { "aeiouAEIOU", r_circ },    { "aeiouyAEIOUY", r_diaer },
};
static int compose(int dead, int ch) {
    const char *b = comp[dead].base;
    for (int i = 0; b[i]; i++) if ((u8)b[i] == ch) return comp[dead].res[i];
    return 0;
}

int g_kbd_dead;
static int sh_l, sh_r, ctl, alt, altgr, caps, numlock = 1, ext, pause_skip;

static void emit(int key, int mods) { event_t e = { EV_KEY, key, mods, 0, 0 }; push(e); }
#define NK_WIN 0x130

static void kbd_irq(void) {
    u8 sc = inb(0x60);
    if (sc == 0xE0) { ext = 1; return; }
    if (sc == 0xE1) { pause_skip = 5; return; }
    if (pause_skip) { pause_skip--; return; }
    int rel = sc & 0x80; sc &= 0x7F;
    int e = ext; ext = 0;

    /* modificadores */
    if (!e && sc == 0x2A) { sh_l = !rel; return; }
    if (!e && sc == 0x36) { sh_r = !rel; return; }
    if (sc == 0x1D) { ctl = !rel; return; }
    if (sc == 0x38) { if (e) altgr = !rel; else alt = !rel; return; }
    if (rel) return;
    if (!e && sc == 0x3A) { caps = !caps; return; }
    if (!e && sc == 0x45) { numlock = !numlock; return; }

    int shift = sh_l || sh_r;
    int mods = (shift ? NM_SHIFT : 0) | (ctl ? NM_CTRL : 0) | (alt ? NM_ALT : 0) | (altgr ? NM_ALTGR : 0) | (caps ? NM_CAPS : 0);

    if (e) {
        switch (sc) {
        case 0x48: emit(NK_UP, mods); return;    case 0x50: emit(NK_DOWN, mods); return;
        case 0x4B: emit(NK_LEFT, mods); return;  case 0x4D: emit(NK_RIGHT, mods); return;
        case 0x47: emit(NK_HOME, mods); return;  case 0x4F: emit(NK_END, mods); return;
        case 0x49: emit(NK_PGUP, mods); return;  case 0x51: emit(NK_PGDN, mods); return;
        case 0x52: emit(NK_INSERT, mods); return;
        case 0x53:
            if (ctl && alt) system_reboot();
            emit(NK_DELETE, mods); return;
        case 0x1C: emit(NK_ENTER, mods); return;
        case 0x35: emit('/', mods); return;
        case 0x5B: case 0x5C: emit(NK_WIN, mods); return;
        }
        return;
    }
    /* teclas de funcao e especiais */
    if (sc >= 0x3B && sc <= 0x44) { emit(NK_F1 + (sc - 0x3B), mods); return; }
    if (sc == 0x57) { emit(NK_F11, mods); return; }
    if (sc == 0x58) { emit(NK_F12, mods); return; }
    switch (sc) {
    case 0x01: emit(NK_ESC, mods); return;
    case 0x0E: emit(NK_BACKSPACE, mods); return;
    case 0x0F: emit(NK_TAB, mods); return;
    case 0x1C: emit(NK_ENTER, mods); return;
    case 0x37: emit('*', mods); return;
    case 0x4A: emit('-', mods); return;
    case 0x4E: emit('+', mods); return;
    case 0x7E: emit('.', mods); return;
    }
    /* teclado numerico */
    if (sc >= 0x47 && sc <= 0x53 && sc != 0x4A && sc != 0x4E) {
        static const char digs[] = "789?456?1230,";
        static const int navs[] = { NK_HOME, NK_UP, NK_PGUP, 0, NK_LEFT, 0, NK_RIGHT, 0, NK_END, NK_DOWN, NK_PGDN, NK_INSERT, NK_DELETE };
        int k = sc - 0x47;
        if (numlock) emit(digs[k], mods); else if (navs[k]) emit(navs[k], mods);
        return;
    }

    keydef_t t = abnt2[sc];
    if (!t.n) return;
    int is_letter = (t.n >= 'a' && t.n <= 'z') || t.n == 0xE7;
    int ch;
    if (ctl && !altgr) {                      /* atalhos: sempre a letra base */
        if (t.n < 0x20) return;
        emit(t.n, mods); return;
    }
    if (altgr && t.g) ch = t.g;
    else {
        int up = shift; if (is_letter && caps) up ^= 1;
        ch = up ? t.s : t.n;
    }
    if (!ch) return;
    if (ch <= D_DIAER) {                      /* tecla morta */
        if (g_kbd_dead) emit(accent_char[g_kbd_dead], mods);
        g_kbd_dead = ch; return;
    }
    if (g_kbd_dead) {
        int d = g_kbd_dead; g_kbd_dead = 0;
        if (ch == ' ') { emit(accent_char[d], mods); return; }
        int c = compose(d, ch);
        if (c) { emit(c, mods); return; }
        emit(accent_char[d], mods);
    }
    emit(ch, mods);
}

/* ================= mouse ================= */
static int mstate, mouse_id;
static u8 mpkt[4];
static void mouse_irq(void) {
    u8 d = inb(0x60);
    switch (mstate) {
    case 0: if (!(d & 8)) return; mpkt[0] = d; mstate = 1; return;
    case 1: mpkt[1] = d; mstate = 2; return;
    case 2: mpkt[2] = d; if (mouse_id == 3 || mouse_id == 4) { mstate = 3; return; } break;
    case 3: mpkt[3] = d; break;
    }
    mstate = 0;
    if (mpkt[0] & 0xC0) return;                          /* overflow */
    int dx = (int)mpkt[1] - ((mpkt[0] << 4) & 0x100);
    int dy = (int)mpkt[2] - ((mpkt[0] << 3) & 0x100);
    int wheel = 0;
    if (mouse_id == 3 || mouse_id == 4) wheel = ((s8)((mpkt[3] & 0x0F) << 4)) >> 4;
    event_t e = { EV_MOUSE, dx, -dy, mpkt[0] & 7, wheel };
    push(e);
}

void input_init(void) {
    kbc_cmd(0xAD); kbc_cmd(0xA7);
    for (int i = 0; i < 64 && (inb(0x64) & 1); i++) inb(0x60);
    kbc_cmd(0x20);
    int cfg = kbc_read(); if (cfg < 0) cfg = 0x45;
    cfg = (cfg & ~0x33) | 0x40;                          /* IRQs desligadas, clocks ligados, traducao set 1 */
    kbc_cmd(0x60); kbc_data(cfg);
    kbc_cmd(0xAE); kbc_cmd(0xA8);
    kbc_data(0xF4); kbc_read();                          /* teclado: habilita varredura */
    mouse_cmd(0xF6);
    mouse_cmd(0xF3); mouse_cmd(200);
    mouse_cmd(0xF3); mouse_cmd(100);
    mouse_cmd(0xF3); mouse_cmd(80);                      /* sequencia magica do IntelliMouse */
    mouse_cmd(0xF2); mouse_id = kbc_read();
    if (mouse_id != 3 && mouse_id != 4) mouse_id = 0;
    mouse_cmd(0xF3); mouse_cmd(100);
    mouse_cmd(0xF4);
    for (int i = 0; i < 64 && (inb(0x64) & 1); i++) inb(0x60);
    kbc_cmd(0x60); kbc_data(cfg | 3);                    /* habilita IRQ1 e IRQ12 */
    irq_register(1, kbd_irq);
    irq_register(12, mouse_irq);
    kprintf("[input] teclado ABNT2, mouse id=%d\n", mouse_id);
}
