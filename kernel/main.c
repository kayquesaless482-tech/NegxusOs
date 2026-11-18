/* NegxusOS - inicializacao do kernel e laco principal */
#include "kernel.h"
#include "wm.h"

const bootinfo_t *g_boot;

/* tela de inicializacao com barra de progresso */
void wm_splash(int percent, const char *msg) {
    nx_canvas *c = &g_screen;
    if (!c->px) return;
    cv_clip(c, 0, 0, c->w, c->h);
    cv_gradient_v(c, 0, 0, c->w, c->h, RGB(10, 30, 70), RGB(30, 90, 170));
    int cx = c->w / 2, cy = c->h / 2;
    int s = 36;
    extern const u32 g_logo40[2304];
    cv_icon(c, g_logo40, cx - 40, cy - 120, 80);
    cv_text(c, cx - text_width("NegxusOS", NF_H1) / 2, cy + 16, "NegxusOS", NF_H1, RGB(255, 255, 255));
    int bw = 320, bx = cx - bw / 2, by = cy + 70;
    cv_rrect(c, bx, by, bw, 4, 2, RGB(20, 60, 120));
    cv_rrect(c, bx, by, bw * CLAMP(percent, 0, 100) / 100 + 1, 4, 2, RGB(255, 255, 255));
    cv_text(c, cx - text_width(msg, NF_UI) / 2, by + 16, msg, NF_UI, RGB(200, 220, 245));
    gfx_present(0, 0, c->w, c->h);
}

/* animacao de inicio: logo cresce com brilho e pulsa antes da tela de progresso */
static void boot_animation(void) {
    nx_canvas *c = &g_screen;
    extern const u32 g_logo40[2304];
    u32 next = g_ticks + 16;
    for (int f = 0; f <= 90; f++) {
        cv_clip(c, 0, 0, c->w, c->h);
        cv_gradient_v(c, 0, 0, c->w, c->h, RGB(6, 18, 44), RGB(20, 70, 150));
        int size = 24 + f * 96 / 90;
        if (f > 60) size += ((f - 60) % 10 < 5) ? 2 : 0;          /* pulso final */
        int cx = c->w / 2, cy = c->h / 2 - 20;
        cv_icon(c, g_logo40, cx - size / 2, cy - size / 2, size);
        int bw = 220, bx = cx - bw / 2, by = cy + 90;
        cv_rrect(c, bx, by, bw, 3, 1, RGB(20, 60, 120));
        cv_rrect(c, bx, by, bw * f / 90 + 1, 3, 1, RGB(200, 225, 255));
        gfx_present(0, 0, c->w, c->h);
        while ((s32)(g_ticks - next) < 0) cpu_idle();
        next += 16;
    }
}

void kmain(const bootinfo_t *bi) {
    g_boot = bi;
    serial_init();
    kprintf("\nNegxusOS 1.0 - inicializando\n");
    arch_init();
    mem_init(bi);
    gfx_init(bi);
    boot_animation();
    ring3_selftest();
    wm_splash(5, "Iniciando o nucleo");
    input_init();
    wm_splash(25, "Carregando teclado e mouse");
    fs_init();
    nxe_init();
    wm_splash(50, "Carregando aplicativos");
    wm_init();
    wm_splash(100, "Pronto");
    kprintf("[sys] pronto\n");
    for (;;) {
        event_t e;
        int any = 0;
        while (input_next(&e)) { wm_handle_event(&e); any = 1; }
        wm_tick();
        if (wm_needs_redraw()) wm_redraw();
        else if (!any) cpu_idle();
    }
}
