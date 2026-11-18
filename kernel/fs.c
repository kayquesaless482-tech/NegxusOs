/* NegxusOS - sistema de arquivos em memoria (NegxFS-RAM) + carga do initrd (NXAR) */
#include "kernel.h"

typedef struct node {
    char name[60]; u8 is_dir;
    struct node *parent, *child, *next;
    u8 *data; u32 size, cap;
} node_t;

static node_t root;

#define KTAG_BEGIN int _old = g_alloc_tag; g_alloc_tag = 0
#define KTAG_END   g_alloc_tag = _old

static int name_eq(const char *a, const char *b, int bl) {
    return (int)strlen(a) == bl && strncasecmp(a, b, bl) == 0;
}
static node_t *find_child(node_t *d, const char *n, int len) {
    for (node_t *c = d->child; c; c = c->next) if (name_eq(c->name, n, len)) return c;
    return 0;
}

/* Resolve caminho; se parent != 0, resolve apenas ate o diretorio pai e devolve o nome final */
static node_t *resolve(const char *path, const char **leaf, int *leaflen) {
    node_t *cur = &root;
    const char *p = path;
    while (*p == '/') p++;
    for (;;) {
        const char *e = p;
        while (*e && *e != '/') e++;
        int len = (int)(e - p);
        const char *after = e; while (*after == '/') after++;
        int last = (*after == 0);
        if (len == 0) { if (leaf) { *leaf = p; *leaflen = 0; } return cur; }
        if (leaf && last) { *leaf = p; *leaflen = len; return cur; }
        if (len == 1 && p[0] == '.') { /* nada */ }
        else if (len == 2 && p[0] == '.' && p[1] == '.') { if (cur->parent) cur = cur->parent; }
        else {
            node_t *c = find_child(cur, p, len);
            if (!c) return 0;
            cur = c;
        }
        if (last) { if (leaf) { *leaf = p; *leaflen = 0; } return cur; }
        p = after;
    }
}
static node_t *lookup(const char *path) { return resolve(path, 0, 0); }

static node_t *new_node(node_t *parent, const char *name, int len, int is_dir) {
    node_t *n = kzalloc(sizeof(node_t));
    if (!n) return 0;
    if (len > 59) len = 59;
    memcpy(n->name, name, len); n->name[len] = 0;
    n->is_dir = is_dir; n->parent = parent;
    node_t **pp = &parent->child;            /* anexa no fim */
    while (*pp) pp = &(*pp)->next;
    *pp = n;
    return n;
}
static void free_node(node_t *n) {
    while (n->child) { node_t *c = n->child; n->child = c->next; free_node(c); }
    if (n->data) kfree(n->data);
    kfree(n);
}
static void detach(node_t *n) {
    node_t **pp = &n->parent->child;
    while (*pp && *pp != n) pp = &(*pp)->next;
    if (*pp) *pp = n->next;
    n->next = 0;
}

int fs_mkdir(const char *path) {
    KTAG_BEGIN; int r = -1;
    const char *leaf; int ll;
    node_t *par = resolve(path, &leaf, &ll);
    if (par && par->is_dir && ll > 0 && ll < 60 && !find_child(par, leaf, ll) && !(ll == 1 && leaf[0] == '.')) {
        r = new_node(par, leaf, ll, 1) ? 0 : -1;
    }
    KTAG_END; return r;
}

int fs_write(const char *path, const void *buf, int n) {
    KTAG_BEGIN; int r = -1;
    const char *leaf; int ll;
    node_t *par = resolve(path, &leaf, &ll);
    if (par && par->is_dir && ll > 0 && ll < 60) {
        node_t *f = find_child(par, leaf, ll);
        if (f && f->is_dir) goto out;
        if (!f) f = new_node(par, leaf, ll, 0);
        if (!f) goto out;
        u8 *nd = kmalloc(n ? n : 1);
        if (!nd) goto out;
        memcpy(nd, buf, n);
        if (f->data) kfree(f->data);
        f->data = nd; f->size = n; f->cap = n;
        r = n;
    }
out:
    KTAG_END; return r;
}

int fs_read(const char *path, void *buf, int max) {
    node_t *f = lookup(path);
    if (!f || f->is_dir) return -1;
    int n = (int)MIN(f->size, (u32)max);
    if (n > 0) memcpy(buf, f->data, n);
    return n;
}
void *fs_load(const char *path, int *size) {
    node_t *f = lookup(path);
    if (!f || f->is_dir) return 0;
    u8 *b = kmalloc(f->size + 1);
    if (!b) return 0;
    if (f->size) memcpy(b, f->data, f->size);
    b[f->size] = 0;
    if (size) *size = (int)f->size;
    return b;
}
int fs_stat(const char *path, nx_stat *st) {
    node_t *f = lookup(path);
    if (!f) return -1;
    st->is_dir = f->is_dir; st->size = f->size;
    return 0;
}
int fs_list(const char *path, nx_dirent *out, int max) {
    node_t *d = lookup(path);
    if (!d || !d->is_dir) return -1;
    int n = 0;
    for (node_t *c = d->child; c && n < max; c = c->next, n++) {
        strncpy(out[n].name, c->name, 59); out[n].name[59] = 0;
        out[n].size = c->size; out[n].is_dir = c->is_dir;
    }
    return n;
}
int fs_remove(const char *path) {
    KTAG_BEGIN; int r = -1;
    node_t *n = lookup(path);
    if (n && n != &root) { detach(n); free_node(n); r = 0; }
    KTAG_END; return r;
}
int fs_rename(const char *from, const char *to) {
    node_t *n = lookup(from);
    if (!n || n == &root) return -1;
    const char *leaf; int ll;
    node_t *par = resolve(to, &leaf, &ll);
    if (!par || !par->is_dir || ll <= 0 || ll > 59) return -1;
    node_t *ex = find_child(par, leaf, ll);
    if (ex && ex != n) return -1;
    for (node_t *a = par; a; a = a->parent) if (a == n) return -1;     /* nao mover para dentro de si */
    detach(n);
    n->parent = par;
    memcpy(n->name, leaf, ll); n->name[ll] = 0;
    node_t **pp = &par->child;
    while (*pp) pp = &(*pp)->next;
    *pp = n;
    return 0;
}

/* ---------------- initrd: "NXAR", u32 n, { u16 pathlen, u8 isdir, u8 pad, u32 size, path, data } ---------------- */
void fs_init(void) {
    memset(&root, 0, sizeof root);
    root.is_dir = 1;
    const u8 *p = (const u8 *)__initrd_start;
    if (memcmp(p, "NXAR", 4)) { kprintf("[fs] initrd invalido\n"); return; }
    u32 count = *(const u32 *)(p + 4);
    p += 8;
    for (u32 i = 0; i < count; i++) {
        u16 pl = *(const u16 *)p; u8 isdir = p[2]; u32 size = *(const u32 *)(p + 4);
        char path[160];
        if (pl > 150) pl = 150;
        memcpy(path, p + 8, pl); path[pl] = 0;
        p += 8 + ((pl + 3) & ~3u);
        if (isdir) fs_mkdir(path);
        else { fs_write(path, p, size); p += (size + 3) & ~3u; }
    }
    kprintf("[fs] initrd: %d entradas\n", (int)count);
}
