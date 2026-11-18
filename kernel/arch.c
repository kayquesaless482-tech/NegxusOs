/* NegxusOS - arquitetura x86-64: serial, GDT, IDT, PIC, PIT, RTC, energia */
#include "kernel.h"

extern void gdt_flush(void *gdtr);
extern u64 isr_stub_table[48];
volatile u32 g_ticks;

/* ---------------- serial (COM1) para depuracao ---------------- */
void serial_init(void) {
    outb(0x3F9, 0); outb(0x3FB, 0x80); outb(0x3F8, 1); outb(0x3F9, 0);
    outb(0x3FB, 0x03); outb(0x3FA, 0xC7); outb(0x3FC, 0x0B);
}
static void serial_putc(char c) {
    for (int i = 0; i < 20000 && !(inb(0x3FD) & 0x20); i++) ;
    outb(0x3F8, (u8)c);
}
void kputs(const char *s) { for (; *s; s++) { if (*s == '\n') serial_putc('\r'); serial_putc(*s); } }
void kprintf(const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt);
    nx_vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    kputs(buf);
}

/* ---------------- GDT / IDT ---------------- */
typedef struct { u16 limit; u64 base; } __attribute__((packed)) dtr_t;
typedef struct { u16 o0; u16 sel; u8 ist; u8 attr; u16 o1; u32 o2; u32 zero; } __attribute__((packed)) idt_entry_t;
/* 0 nulo | 1 codigo kernel 0x08 | 2 dados kernel 0x10 | 3 dados usuario 0x1B | 4 codigo usuario 0x23 | 5-6 TSS 0x28 */
static u64 gdt[7] = { 0, 0x00AF9A000000FFFFULL, 0x00CF92000000FFFFULL, 0x00CFF2000000FFFFULL, 0x00AFFA000000FFFFULL, 0, 0 };
typedef struct { u32 reserved0; u64 rsp0, rsp1, rsp2; u64 reserved1; u64 ist[7]; u64 reserved2; u16 reserved3; u16 iomap; } __attribute__((packed)) tss_t;
static tss_t tss;
/* pilha de interrupcao do anel 3: separada da pilha do kernel (senao sobrescreve os quadros ativos) */
static u8 ring0_stack[16384] __attribute__((aligned(16)));
static idt_entry_t idt[256];
static void (*irq_handlers[16])(void);

static void idt_set(int n, u64 fn) {
    idt[n].o0 = fn & 0xFFFF; idt[n].sel = 0x08; idt[n].ist = 0; idt[n].attr = 0x8E;
    idt[n].o1 = (fn >> 16) & 0xFFFF; idt[n].o2 = fn >> 32; idt[n].zero = 0;
}
void irq_register(int irq, void (*fn)(void)) { irq_handlers[irq] = fn; }

static const char *exc_names[32] = {
    "Divisao por zero", "Debug", "NMI", "Breakpoint", "Overflow", "Limite excedido", "Instrucao invalida",
    "Dispositivo indisponivel", "Falha dupla", "Segmento", "TSS invalido", "Segmento ausente", "Falha de pilha",
    "Protecao geral", "Falha de pagina", "Reservado", "x87", "Alinhamento", "Machine check", "SIMD", "Virtualizacao",
    "Controle", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-" };

void isr_dispatch(regs_t *r) {
    if (r->int_no == 0x80) { syscall_dispatch(r); return; }
    if (r->int_no < 32) {
        kprintf("\n*** EXCECAO %d (%s) rip=%lx err=%lx\n", (int)r->int_no, exc_names[r->int_no], r->rip, r->err);
        panic_screen(exc_names[r->int_no], r);
        for (;;) asm volatile("cli; hlt");
    }
    int irq = (int)r->int_no - 32;
    if (irq == 7 || irq == 15) {                       /* possivel IRQ espuria */
        outb(irq == 15 ? 0xA0 : 0x20, 0x0B);
        if (!(inb(irq == 15 ? 0xA0 : 0x20) & 0x80)) { if (irq == 15) outb(0x20, 0x20); return; }
    }
    if (irq_handlers[irq]) irq_handlers[irq]();
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static void timer_irq(void) { g_ticks++; }

static void gdt_set_tss(void) {
    u64 base = (u64)&tss, limit = sizeof tss - 1;
    gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ULL << 40) | (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    gdt[6] = (base >> 32) & 0xFFFFFFFF;
    tss.rsp0 = (u64)(ring0_stack + sizeof ring0_stack);
    tss.iomap = sizeof tss;
}
void arch_init(void) {
    gdt_set_tss();
    dtr_t g = { sizeof gdt - 1, (u64)gdt };
    gdt_flush(&g);
    asm volatile("ltr %%ax" : : "a"((u16)0x28));
    for (int i = 0; i < 48; i++) idt_set(i, isr_stub_table[i]);
    extern void isr128(void);
    idt_set(0x80, (u64)isr128);
    idt[0x80].attr = 0xEE;                       /* porta de syscall: acessivel pelo anel 3 (DPL 3) */
    dtr_t d = { sizeof idt - 1, (u64)idt };
    asm volatile("lidt %0" : : "m"(d));
    /* PIC: remapeia para 32..47 */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 32);   outb(0xA1, 40);
    outb(0x21, 4);    outb(0xA1, 2);
    outb(0x21, 1);    outb(0xA1, 1);
    outb(0x21, 0xF8); outb(0xA1, 0xEF);                /* libera IRQ0, 1, 2(cascata), 12 */
    /* PIT a ~1000 Hz */
    outb(0x43, 0x34); outb(0x40, 1193 & 0xFF); outb(0x40, 1193 >> 8);
    irq_register(0, timer_irq);
    sti();
}

void cpu_idle(void) { asm volatile("sti; hlt" ::: "memory"); }

/* ---------------- paginas sem cache (MMIO) ---------------- */
void map_uncached(u64 phys, u64 len) {
    volatile u64 *pd = (volatile u64 *)0x72000;       /* PDs contiguas criadas pelo estagio 2 */
    for (u64 a = phys & ~0x1FFFFFULL; a < phys + len && (a >> 21) < 2048; a += 0x200000)
        pd[a >> 21] |= (1 << 3) | (1 << 4);
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
}

/* ---------------- aleatorio ---------------- */
u32 rand32(void) {
    static u64 s;
    if (!s) s = rdtsc() ^ 0x9E3779B97F4A7C15ULL;
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return (u32)(s >> 16);
}

/* ---------------- RTC ---------------- */
static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
void rtc_read(nx_time *t) {
    for (int i = 0; i < 1000 && (cmos(0x0A) & 0x80); i++) ;
    u8 s = cmos(0), m = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9), b = cmos(0x0B);
    if (!(b & 4)) {
        s = (s & 15) + (s >> 4) * 10; m = (m & 15) + (m >> 4) * 10;
        h = ((h & 15) + ((h & 0x70) >> 4) * 10) | (h & 0x80);
        d = (d & 15) + (d >> 4) * 10; mo = (mo & 15) + (mo >> 4) * 10; y = (y & 15) + (y >> 4) * 10;
    }
    if (!(b & 2) && (h & 0x80)) h = ((h & 0x7F) + 12) % 24;
    t->sec = s; t->min = m; t->hour = h; t->day = d; t->month = mo; t->year = 2000 + y;
}

/* ---------------- energia ---------------- */
void system_reboot(void) {
    cli();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) ;
    outb(0x64, 0xFE);
    for (int i = 0; i < 1000000; i++) asm volatile("pause");
    dtr_t z = { 0, 0 };
    asm volatile("lidt %0; int3" : : "m"(z));
    for (;;) asm volatile("hlt");
}
void system_poweroff(void) {
    outw(0x604, 0x2000);     /* QEMU / ACPI padrao de VMs */
    outw(0xB004, 0x2000);    /* Bochs / QEMU antigo */
    outw(0x4004, 0x3400);    /* VirtualBox */
    outw(0x600, 0x34);
}

/* ---------------- tela de erro ---------------- */
void panic_screen(const char *msg, regs_t *r) {
    if (!g_screen.px) return;
    nx_canvas *c = &g_screen;
    cv_clip(c, 0, 0, c->w, c->h);
    cv_fill(c, 0, 0, c->w, c->h, RGB(0, 70, 140));
    char b[160];
    cv_text(c, 60, 60, ":(", NF_H1, 0xFFFFFF);
    cv_text(c, 60, 110, "O NegxusOS encontrou um problema e precisou parar.", NF_H2, 0xFFFFFF);
    nx_snprintf(b, sizeof b, "Codigo: %s (excecao %d)", msg, (int)r->int_no);
    cv_text(c, 60, 160, b, NF_UI, 0xFFFFFF);
    u64 cr2; asm volatile("mov %%cr2, %0" : "=r"(cr2));
    nx_snprintf(b, sizeof b, "RIP=%lx  ERR=%lx  CR2=%lx", r->rip, r->err, cr2);
    cv_text(c, 60, 185, b, NF_MONO, 0xFFFFFF);
    nx_snprintf(b, sizeof b, "RSP=%lx  RAX=%lx  RBX=%lx  RCX=%lx", r->rsp, r->rax, r->rbx, r->rcx);
    cv_text(c, 60, 205, b, NF_MONO, 0xFFFFFF);
    cv_text(c, 60, 250, "Reinicie o computador para continuar.", NF_UI, 0xFFFFFF);
    gfx_present(0, 0, c->w, c->h);
}
