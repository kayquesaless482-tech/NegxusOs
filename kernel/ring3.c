/* NegxusOS - anel 3 (modo usuario): segmentos, paginas de usuario, syscalls e teste */
#include "kernel.h"

#define USER_BASE 0x400000ULL          /* pagina de 2 MiB dedicada ao codigo/pilha do teste */
#define USER_STACK_TOP (USER_BASE + 0x1F0000ULL)

static u64 saved_rsp;
static void *ret_label;
static u64 saved_regs[6];          /* rbx, rbp, r12..r15 do chamador (kmain) */

/* chamadas do anel 3: eax = numero, rdi = argumento */
void syscall_dispatch(regs_t *r) {
    switch (r->rax) {
    case 1:                                   /* putc(caractere) */
        outb(0x3F8, (u8)r->rdi);
        r->rax = 0;
        break;
    case 2:                                   /* exit(codigo): volta ao kernel */
        kprintf("\n[ring3] processo saiu com codigo %d\n", (int)r->rdi);
        r->rax = 0;
        asm volatile("mov %0, %%rsp\n\tjmp *%1" : : "m"(saved_rsp), "r"(ret_label) : "memory");
        for (;;) asm volatile("hlt");
    default:
        r->rax = (u64)-1;
        break;
    }
}

/* marca a pagina de usuario: U/S em PML4, PDPT e na entrada de 2 MiB */
static void map_user_page(u64 addr) {
    volatile u64 *pml4 = (volatile u64 *)0x70000;
    volatile u64 *pdpt = (volatile u64 *)0x71000;
    volatile u64 *pd   = (volatile u64 *)0x72000;
    pml4[0] |= 4;
    pdpt[0] |= 4;
    pd[addr >> 21] |= 4;
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
}

/* codigo do processo de teste (x86-64):
   putc('R'), putc('I'), exit(0), jmp $  */
static const u8 user_blob[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,   /* mov eax,1  (putc)  */
    0xBF, 0x52, 0x00, 0x00, 0x00,   /* mov edi,'R'        */
    0xCD, 0x80,                     /* int 0x80           */
    0xB8, 0x01, 0x00, 0x00, 0x00,   /* mov eax,1  (rax e' sobrescrito pelo retorno) */
    0xBF, 0x49, 0x00, 0x00, 0x00,   /* mov edi,'I'        */
    0xCD, 0x80,                     /* int 0x80           */
    0xB8, 0x02, 0x00, 0x00, 0x00,   /* mov eax,2  (exit)  */
    0x31, 0xFF,                     /* xor edi,edi        */
    0xCD, 0x80,                     /* int 0x80           */
    0xEB, 0xFE                      /* jmp $              */
};

void ring3_selftest(void) {
    map_user_page(USER_BASE);
    memcpy((void *)USER_BASE, user_blob, sizeof user_blob);
    asm volatile("mov %%rsp, %0" : "=m"(saved_rsp) : : "memory");
    asm volatile("movq %%rbx, %0\n\tmovq %%rbp, %1\n\tmovq %%r12, %2\n\tmovq %%r13, %3\n\tmovq %%r14, %4\n\tmovq %%r15, %5"
                 : "=m"(saved_regs[0]), "=m"(saved_regs[1]), "=m"(saved_regs[2]), "=m"(saved_regs[3]), "=m"(saved_regs[4]), "=m"(saved_regs[5]) : : "memory");
    ret_label = &&back;
    u64 rip = USER_BASE, rsp = USER_STACK_TOP;
    asm volatile(
        "mov $0x1B, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "pushq $0x1B\n\t"          /* SS  */
        "pushq %0\n\t"             /* RSP */
        "pushfq\n\t"
        "orq $0x200, (%%rsp)\n\t"  /* IF = 1 */
        "pushq $0x23\n\t"          /* CS  */
        "pushq %1\n\t"             /* RIP */
        "iretq"
        : : "r"(rsp), "r"(rip) : "rax", "memory");
back:
    asm volatile("movq %0, %%rbx\n\tmovq %1, %%rbp\n\tmovq %2, %%r12\n\tmovq %3, %%r13\n\tmovq %4, %%r14\n\tmovq %5, %%r15"
                 : : "m"(saved_regs[0]), "m"(saved_regs[1]), "m"(saved_regs[2]), "m"(saved_regs[3]), "m"(saved_regs[4]), "m"(saved_regs[5]) : "memory");
    asm volatile("sti" ::: "memory");
    kprintf("[ring3] teste concluido: ok\n");
}
