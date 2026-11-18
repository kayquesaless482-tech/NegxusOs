/* NegxusOS - gerenciador de memoria: heap first-fit com etiquetas por aplicativo */
#include "kernel.h"

typedef struct blk {
    u64 size; u32 used, tag; struct blk *prev, *next; u64 magic, pad;
} blk_t;
#define HDR 48
#define MAGIC 0x4E45475855534F53ULL

static blk_t *head;
static u64 heap_lo, heap_hi, total_usable;
int g_alloc_tag;

void mem_init(const bootinfo_t *bi) {
    u64 best_lo = 0, best_len = 0;
    total_usable = 0;
    for (u32 i = 0; i < bi->mmap_count && i < 48; i++) {
        const mmap_entry_t *e = &bi->mmap[i];
        if (e->type != 1) continue;
        total_usable += e->len;
        u64 lo = e->base, hi = e->base + e->len;
        if (lo < 0x1000000) lo = 0x1000000;            /* heap comeca em 16 MiB */
        if (hi > 0xC0000000ULL) hi = 0xC0000000ULL;    /* abaixo de 3 GiB (identidade + DMA 32 bits) */
        if (hi > lo && hi - lo > best_len) { best_lo = lo; best_len = hi - lo; }
    }
    if (best_len > (768ULL << 20)) best_len = 768ULL << 20;
    heap_lo = (best_lo + 4095) & ~4095ULL;
    heap_hi = (best_lo + best_len) & ~4095ULL;
    head = (blk_t *)heap_lo;
    head->size = heap_hi - heap_lo - HDR; head->used = 0; head->tag = 0;
    head->prev = head->next = 0; head->magic = MAGIC;
    kprintf("[mem] heap %lx..%lx (%d MiB)\n", heap_lo, heap_hi, (int)((heap_hi - heap_lo) >> 20));
}

void *kmalloc(size_t n) {
    if (!n) n = 1;
    n = (n + 15) & ~15ULL;
    for (blk_t *b = head; b; b = b->next) {
        if (b->used || b->size < n) continue;
        if (b->size >= n + HDR + 64) {
            blk_t *r = (blk_t *)((u8 *)b + HDR + n);
            r->size = b->size - n - HDR; r->used = 0; r->tag = 0; r->magic = MAGIC;
            r->prev = b; r->next = b->next;
            if (b->next) b->next->prev = r;
            b->next = r; b->size = n;
        }
        b->used = 1; b->tag = g_alloc_tag;
        return (u8 *)b + HDR;
    }
    kprintf("[mem] SEM MEMORIA (%lu bytes)\n", (u64)n);
    return 0;
}
void *kzalloc(size_t n) { void *p = kmalloc(n); if (p) memset(p, 0, n); return p; }

void kfree(void *p) {
    if (!p) return;
    blk_t *b = (blk_t *)((u8 *)p - HDR);
    if (b->magic != MAGIC || !b->used) { kprintf("[mem] kfree invalido %lx\n", (u64)p); return; }
    b->used = 0; b->tag = 0;
    if (b->next && !b->next->used) {
        blk_t *n = b->next;
        b->size += HDR + n->size; b->next = n->next;
        if (n->next) n->next->prev = b;
    }
    if (b->prev && !b->prev->used) {
        blk_t *pr = b->prev;
        pr->size += HDR + b->size; pr->next = b->next;
        if (b->next) b->next->prev = pr;
    }
}

void *krealloc(void *p, size_t n) {
    if (!p) return kmalloc(n);
    blk_t *b = (blk_t *)((u8 *)p - HDR);
    if (b->size >= n) return p;
    int old = g_alloc_tag; g_alloc_tag = b->tag;
    void *q = kmalloc(n);
    g_alloc_tag = old;
    if (!q) return 0;
    memcpy(q, p, b->size);
    kfree(p);
    return q;
}

void mem_free_tag(int tag) {
    if (!tag) return;
    int again = 1;
    while (again) {
        again = 0;
        for (blk_t *b = head; b; b = b->next)
            if (b->used && b->tag == (u32)tag) { kfree((u8 *)b + HDR); again = 1; break; }
    }
}
u32 mem_total_mb(void) { return (u32)(total_usable >> 20) + 1; }
u32 mem_free_kb(void) {
    u64 s = 0;
    for (blk_t *b = head; b; b = b->next) if (!b->used) s += b->size;
    return (u32)(s >> 10);
}
