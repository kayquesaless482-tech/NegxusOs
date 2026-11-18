/* NegxusOS - FAT16 persistente com nomes longos VFAT.
 * A particao FAT16 e criada por tools/mkimage.py. Os dados ficam no disco,
 * nao apenas no heap do kernel. O initrd e copiado para o disco no primeiro boot.
 */
#include "kernel.h"

#define FAT16_TYPE          0x06
#define FAT16_SECTOR_SIZE   512
#define FAT16_ATTR_READONLY 0x01
#define FAT16_ATTR_VOLUME   0x08
#define FAT16_ATTR_DIR      0x10
#define FAT16_ATTR_LFN      0x0F
#define FAT16_EOC           0xFFF8
#define FAT16_BAD           0xFFF7
#define FAT16_MAX_FAT_SECT  1024
#define FAT16_MAX_DEPTH     32
#define FAT16_NAME_MAX      59

static u32 part_lba, part_sectors;
static u32 fat_lba, root_lba, data_lba;
static u32 root_entries, root_sectors, fat_sectors;
static u32 sectors_per_cluster, cluster_count, max_cluster;
static u8 fat_count;
static u8 mounted;
static u8 *fat_cache;
static u32 fat_cache_bytes;
static u8 sector_cache[512] __attribute__((aligned(2)));
static u32 sector_cache_lba = 0xFFFFFFFF;

static u16 rd16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static const u16 cp1252_high[32] = {
    0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
    0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
    0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
    0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178
};
static u16 cp1252_decode(u8 c) { return (c >= 0x80 && c <= 0x9F) ? cp1252_high[c - 0x80] : c; }
static u8 cp1252_encode(u16 c) {
    if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) return (u8)c;
    for (u32 i = 0; i < 32; i++) if (cp1252_high[i] == c) return (u8)(i + 0x80);
    return '?';
}
static int ascii_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }
static int name_equal(const char *a, const char *b) {
    while (*a && *b) { if (ascii_lower((u8)*a) != ascii_lower((u8)*b)) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static u16 fat_get(u16 cluster) {
    u32 off = (u32)cluster * 2;
    if (!fat_cache || off + 1 >= fat_cache_bytes) return FAT16_BAD;
    return rd16(fat_cache + off);
}
static void fat_set(u16 cluster, u16 value) {
    u32 off = (u32)cluster * 2;
    if (fat_cache && off + 1 < fat_cache_bytes) wr16(fat_cache + off, value);
}
static int fat_flush(void) {
    if (!fat_cache) return -1;
    for (u32 i = 0; i < fat_count; i++)
        if (disk_write(fat_lba + i * fat_sectors, fat_sectors, fat_cache) != 0) return -1;
    return 0;
}
static u32 cluster_lba(u16 cluster) {
    return data_lba + ((u32)cluster - 2) * sectors_per_cluster;
}

static int fat_alloc_chain(u32 count, u16 *first_out) {
    if (!count) { *first_out = 0; return 0; }
    if (count > cluster_count) return -1;
    u16 first = 0, prev = 0;
    u32 found = 0;
    for (u32 c = 2; c <= max_cluster && found < count; c++) {
        if (fat_get((u16)c) != 0) continue;
        if (!first) first = (u16)c;
        if (prev) fat_set(prev, (u16)c);
        fat_set((u16)c, 0xFFFF);
        prev = (u16)c;
        found++;
    }
    if (found != count) {
        if (first) {
            u16 c = first;
            for (u32 i = 0; i < found; i++) {
                u16 next = fat_get(c);
                fat_set(c, 0);
                if (next >= FAT16_EOC || next < 2) break;
                c = next;
            }
        }
        return -1;
    }
    *first_out = first;
    return 0;
}

static void fat_free_chain(u16 first) {
    u16 c = first;
    for (u32 i = 0; c >= 2 && c <= max_cluster && i < cluster_count; i++) {
        u16 next = fat_get(c);
        fat_set(c, 0);
        if (next >= FAT16_EOC || next == FAT16_BAD || next < 2 || next > max_cluster) break;
        c = next;
    }
}

static int zero_cluster(u16 cluster) {
    u8 zero[512] __attribute__((aligned(2)));
    memset(zero, 0, sizeof zero);
    u32 lba = cluster_lba(cluster);
    for (u32 s = 0; s < sectors_per_cluster; s++)
        if (disk_write(lba + s, 1, zero) != 0) return -1;
    sector_cache_lba = 0xFFFFFFFF;
    return 0;
}

typedef struct { u16 cluster; u8 root; } fat_dir_t;
typedef struct {
    u8 raw[32];
    char name[60];
    u32 slot;
    fat_dir_t parent;
} fat_item_t;

static u32 dir_capacity(fat_dir_t dir) {
    if (dir.root) return root_entries;
    u32 count = 0;
    u16 c = dir.cluster;
    for (u32 i = 0; c >= 2 && c <= max_cluster && i < cluster_count; i++) {
        count += sectors_per_cluster * 16;
        u16 next = fat_get(c);
        if (next >= FAT16_EOC || next < 2 || next > max_cluster) break;
        c = next;
    }
    return count;
}

static int dir_slot_lba(fat_dir_t dir, u32 slot, u32 *lba, u32 *offset) {
    if (dir.root) {
        if (slot >= root_entries) return -1;
        *lba = root_lba + slot / 16;
        *offset = (slot % 16) * 32;
        return 0;
    }
    u32 per_cluster = sectors_per_cluster * 16;
    u32 skip = slot / per_cluster;
    u32 in_cluster = slot % per_cluster;
    u16 c = dir.cluster;
    for (u32 i = 0; i < skip; i++) {
        u16 next = fat_get(c);
        if (next >= FAT16_EOC || next < 2 || next > max_cluster) return -1;
        c = next;
    }
    if (c < 2 || c > max_cluster) return -1;
    *lba = cluster_lba(c) + in_cluster / 16;
    *offset = (in_cluster % 16) * 32;
    return 0;
}

static int dir_read_slot(fat_dir_t dir, u32 slot, u8 out[32]) {
    u32 lba, off;
    if (dir_slot_lba(dir, slot, &lba, &off) != 0) return -1;
    if (sector_cache_lba != lba) {
        if (disk_read(lba, 1, sector_cache) != 0) return -1;
        sector_cache_lba = lba;
    }
    memcpy(out, sector_cache + off, 32);
    return 0;
}

static int dir_write_slot(fat_dir_t dir, u32 slot, const u8 in[32]) {
    u32 lba, off;
    if (dir_slot_lba(dir, slot, &lba, &off) != 0) return -1;
    u8 temp[512] __attribute__((aligned(2)));
    if (disk_read(lba, 1, temp) != 0) return -1;
    memcpy(temp + off, in, 32);
    sector_cache_lba = 0xFFFFFFFF;
    return disk_write(lba, 1, temp);
}

static int dir_extend(fat_dir_t dir) {
    if (dir.root) return -1;
    u16 c = dir.cluster, last = c;
    for (u32 i = 0; i < cluster_count; i++) {
        u16 next = fat_get(c);
        if (next >= FAT16_EOC) break;
        if (next < 2 || next > max_cluster) return -1;
        c = next; last = c;
    }
    u16 added;
    if (fat_alloc_chain(1, &added) != 0) return -1;
    fat_set(last, added);
    if (zero_cluster(added) != 0) { fat_set(last, 0xFFFF); fat_set(added, 0); return -1; }
    return fat_flush();
}

static int dir_alloc_slots(fat_dir_t dir, u32 needed, u32 *first_slot) {
    if (!needed) return -1;
    for (u32 pass = 0; pass <= cluster_count; pass++) {
        u32 cap = dir_capacity(dir), run = 0, start = 0;
        for (u32 i = 0; i < cap; i++) {
            u8 e[32];
            if (dir_read_slot(dir, i, e) != 0) return -1;
            if (e[0] == 0 || e[0] == 0xE5) {
                if (!run) start = i;
                run++;
                if (run >= needed) { *first_slot = start; return 0; }
            } else run = 0;
        }
        if (dir.root || dir_extend(dir) != 0) return -1;
    }
    return -1;
}

static void short_to_name(const u8 e[32], char out[60]) {
    int n = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++) out[n++] = (char)e[i];
    int ext = 0; for (int i = 8; i < 11; i++) if (e[i] != ' ') ext = 1;
    if (ext && n < 59) out[n++] = '.';
    if (ext) for (int i = 8; i < 11 && e[i] != ' ' && n < 59; i++) out[n++] = (char)e[i];
    out[n] = 0;
}

static void lfn_add(const u8 e[32], char name[60], int *active) {
    static const u8 offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
    u8 ord = e[0];
    u32 seq = ord & 0x1F;
    if (ord & 0x40) { memset(name, 0, 60); *active = 1; }
    if (!*active || seq == 0) return;
    for (u32 i = 0; i < 13; i++) {
        u32 pos = (seq - 1) * 13 + i;
        u16 ch = rd16(e + offsets[i]);
        if (pos >= 59) continue;
        if (ch == 0 || ch == 0xFFFF) name[pos] = 0;
        else name[pos] = (char)cp1252_encode(ch);
    }
}

static int dir_find(fat_dir_t dir, const char *name, fat_item_t *out) {
    u32 cap = dir_capacity(dir);
    char lfn[60]; int active = 0;
    memset(lfn, 0, sizeof lfn);
    for (u32 i = 0; i < cap; i++) {
        u8 e[32];
        if (dir_read_slot(dir, i, e) != 0) return -1;
        if (e[0] == 0) break;
        if (e[0] == 0xE5) { active = 0; memset(lfn, 0, sizeof lfn); continue; }
        if (e[11] == FAT16_ATTR_LFN) { lfn_add(e, lfn, &active); continue; }
        if (e[11] & FAT16_ATTR_VOLUME) { active = 0; continue; }
        char shortname[60];
        if (active && lfn[0]) memcpy(shortname, lfn, sizeof shortname);
        else short_to_name(e, shortname);
        if (name_equal(shortname, name)) {
            memcpy(out->raw, e, 32);
            strncpy(out->name, shortname, 59); out->name[59] = 0;
            out->slot = i; out->parent = dir;
            return 0;
        }
        active = 0; memset(lfn, 0, sizeof lfn);
    }
    return -1;
}

static int dir_short_exists(fat_dir_t dir, const u8 shortname[11]) {
    u32 cap = dir_capacity(dir);
    for (u32 i = 0; i < cap; i++) {
        u8 e[32];
        if (dir_read_slot(dir, i, e) != 0 || e[0] == 0) break;
        if (e[0] == 0xE5 || e[11] == FAT16_ATTR_LFN || (e[11] & FAT16_ATTR_VOLUME)) continue;
        if (!memcmp(e, shortname, 11)) return 1;
    }
    return 0;
}

static int make_short_name(fat_dir_t dir, const char *name, u8 out[11]) {
    const char *dot = 0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    u32 hash = 2166136261u;
    for (const char *p = name; *p; p++) { hash ^= (u8)*p; hash *= 16777619u; }
    memset(out, ' ', 11);
    const char *ext = dot ? dot + 1 : "";
    for (int i = 0; i < 3 && ext[i]; i++) {
        int c = (u8)ext[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out[8 + i] = (u8)c;
    }
    for (u32 attempt = 0; attempt < 100000; attempt++) {
        u32 v = hash + attempt;
        static const char hex[] = "0123456789ABCDEF";
        out[0] = 'N';
        for (int i = 7; i >= 1; i--) { out[i] = (u8)hex[v & 15]; v >>= 4; }
        if (!dir_short_exists(dir, out)) return 0;
    }
    return -1;
}

static u8 lfn_checksum(const u8 shortname[11]) {
    u8 sum = 0;
    for (int i = 0; i < 11; i++) sum = (u8)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + shortname[i]);
    return sum;
}

static int dir_create_entry(fat_dir_t parent, const char *name, u8 attr, u16 cluster, u32 size, u32 *slot_out) {
    int len = (int)strlen(name);
    if (len < 1 || len > FAT16_NAME_MAX) return -1;
    u8 shortname[11];
    if (make_short_name(parent, name, shortname) != 0) return -1;
    u32 lfn_count = ((u32)len + 1 + 12) / 13;
    u32 needed = lfn_count + 1, start;
    if (dir_alloc_slots(parent, needed, &start) != 0) return -1;
    static const u8 offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
    u8 sum = lfn_checksum(shortname);
    for (u32 disk_index = 0; disk_index < lfn_count; disk_index++) {
        u32 ord = lfn_count - disk_index;
        u8 e[32]; memset(e, 0xFF, sizeof e);
        e[0] = (u8)ord | ((ord == lfn_count) ? 0x40 : 0);
        e[11] = FAT16_ATTR_LFN; e[12] = 0; e[13] = sum; wr16(e + 26, 0);
        for (u32 j = 0; j < 13; j++) {
            u32 pos = (ord - 1) * 13 + j;
            u16 ch = (pos < (u32)len) ? cp1252_decode((u8)name[pos]) : ((pos == (u32)len) ? 0 : 0xFFFF);
            wr16(e + offsets[j], ch);
        }
        if (dir_write_slot(parent, start + disk_index, e) != 0) return -1;
    }
    u8 e[32]; memset(e, 0, sizeof e);
    memcpy(e, shortname, 11); e[11] = attr;
    wr16(e + 26, cluster); wr32(e + 28, size);
    if (dir_write_slot(parent, start + lfn_count, e) != 0) return -1;
    if (slot_out) *slot_out = start + lfn_count;
    return 0;
}

static int dir_delete_entry(fat_dir_t parent, u32 short_slot) {
    u8 e[32];
    if (dir_read_slot(parent, short_slot, e) != 0) return -1;
    e[0] = 0xE5;
    if (dir_write_slot(parent, short_slot, e) != 0) return -1;
    while (short_slot > 0) {
        short_slot--;
        if (dir_read_slot(parent, short_slot, e) != 0) return -1;
        if (e[11] != FAT16_ATTR_LFN) break;
        e[0] = 0xE5;
        if (dir_write_slot(parent, short_slot, e) != 0) return -1;
    }
    return 0;
}

static int path_parent(const char *path, fat_dir_t *parent, char leaf[60]) {
    if (!path) return -1;
    fat_dir_t stack[FAT16_MAX_DEPTH];
    int depth = 0;
    stack[0].root = 1; stack[0].cluster = 0;
    const char *p = path;
    while (*p == '/') p++;
    if (!*p) return -1;
    for (;;) {
        char part[60]; int n = 0;
        while (*p && *p != '/') {
            if (n >= FAT16_NAME_MAX) return -1;
            part[n++] = *p++;
        }
        part[n] = 0;
        while (*p == '/') p++;
        int last = (*p == 0);
        if (n == 0) return -1;
        if (name_equal(part, ".")) { if (last) return -1; continue; }
        if (name_equal(part, "..")) {
            if (depth > 0) depth--;
            if (last) return -1;
            continue;
        }
        if (last) {
            memcpy(leaf, part, (size_t)n + 1);
            *parent = stack[depth];
            return 0;
        }
        fat_item_t it;
        if (dir_find(stack[depth], part, &it) != 0 || !(it.raw[11] & FAT16_ATTR_DIR)) return -1;
        u16 c = rd16(it.raw + 26);
        if (c < 2 || c > max_cluster || depth + 1 >= FAT16_MAX_DEPTH) return -1;
        depth++; stack[depth].root = 0; stack[depth].cluster = c;
    }
}

static int resolve_item(const char *path, fat_item_t *item, int *is_root) {
    if (!path) return -1;
    const char *p = path; while (*p == '/') p++;
    if (!*p) { if (is_root) *is_root = 1; return 0; }
    fat_dir_t parent; char leaf[60];
    if (path_parent(path, &parent, leaf) != 0) return -1;
    if (dir_find(parent, leaf, item) != 0) return -1;
    if (is_root) *is_root = 0;
    return 0;
}

static int write_file_chain(u16 first, const void *buf, u32 size) {
    if (!size) return 0;
    const u8 *src = (const u8 *)buf;
    u32 left = size;
    u16 c = first;
    u8 temp[512] __attribute__((aligned(2)));
    while (left) {
        if (c < 2 || c > max_cluster) return -1;
        u32 base = cluster_lba(c);
        for (u32 s = 0; s < sectors_per_cluster && left; s++) {
            u32 n = MIN(left, 512u);
            memset(temp, 0, sizeof temp);
            memcpy(temp, src, n);
            if (disk_write(base + s, 1, temp) != 0) return -1;
            src += n; left -= n;
        }
        if (left) {
            u16 next = fat_get(c);
            if (next >= FAT16_EOC || next < 2) return -1;
            c = next;
        }
    }
    sector_cache_lba = 0xFFFFFFFF;
    return 0;
}

static int read_file_chain(u16 first, u32 size, void *buf, u32 max) {
    u32 left = MIN(size, max);
    u8 *dst = (u8 *)buf;
    u16 c = first;
    u8 temp[512] __attribute__((aligned(2)));
    while (left) {
        if (c < 2 || c > max_cluster) return -1;
        u32 base = cluster_lba(c);
        for (u32 s = 0; s < sectors_per_cluster && left; s++) {
            if (disk_read(base + s, 1, temp) != 0) return -1;
            u32 n = MIN(left, 512u);
            memcpy(dst, temp, n);
            dst += n; left -= n;
        }
        if (left) {
            u16 next = fat_get(c);
            if (next >= FAT16_EOC || next < 2) return -1;
            c = next;
        }
    }
    return (int)MIN(size, max);
}

int fs_mkdir(const char *path) {
    if (!mounted) return -1;
    fat_dir_t parent; char leaf[60];
    if (path_parent(path, &parent, leaf) != 0) return -1;
    fat_item_t exists;
    if (dir_find(parent, leaf, &exists) == 0) return -1;
    u16 cluster;
    if (fat_alloc_chain(1, &cluster) != 0) return -1;
    if (zero_cluster(cluster) != 0) { fat_free_chain(cluster); return -1; }

    fat_dir_t child; child.root = 0; child.cluster = cluster;
    u8 dot[32]; memset(dot, 0, sizeof dot);
    memset(dot, ' ', 11); dot[0] = '.'; dot[11] = FAT16_ATTR_DIR; wr16(dot + 26, cluster);
    if (dir_write_slot(child, 0, dot) != 0) { fat_free_chain(cluster); return -1; }
    memset(dot, 0, sizeof dot); memset(dot, ' ', 11); dot[0] = '.'; dot[1] = '.'; dot[11] = FAT16_ATTR_DIR;
    wr16(dot + 26, parent.root ? 0 : parent.cluster);
    if (dir_write_slot(child, 1, dot) != 0) { fat_free_chain(cluster); return -1; }
    if (dir_create_entry(parent, leaf, FAT16_ATTR_DIR, cluster, 0, 0) != 0) { fat_free_chain(cluster); fat_flush(); return -1; }
    return fat_flush();
}

int fs_write(const char *path, const void *buf, int n) {
    if (!mounted || !path || n < 0 || (n && !buf)) return -1;
    fat_dir_t parent; char leaf[60];
    if (path_parent(path, &parent, leaf) != 0) return -1;
    fat_item_t old; int exists = (dir_find(parent, leaf, &old) == 0);
    if (exists && ((old.raw[11] & FAT16_ATTR_DIR) || (old.raw[11] & FAT16_ATTR_READONLY))) return -1;

    u32 cluster_bytes = sectors_per_cluster * 512;
    u32 clusters_needed = n ? ((u32)n + cluster_bytes - 1) / cluster_bytes : 0;
    u16 first = 0;
    if (clusters_needed && fat_alloc_chain(clusters_needed, &first) != 0) return -1;
    if (clusters_needed && write_file_chain(first, buf, (u32)n) != 0) {
        fat_free_chain(first); fat_flush(); return -1;
    }

    if (exists) {
        u16 old_first = rd16(old.raw + 26);
        wr16(old.raw + 26, first); wr32(old.raw + 28, (u32)n);
        if (dir_write_slot(parent, old.slot, old.raw) != 0) {
            if (first) fat_free_chain(first);
            fat_flush();
            return -1;
        }
        if (old_first) fat_free_chain(old_first);
    } else {
        if (dir_create_entry(parent, leaf, 0x20, first, (u32)n, 0) != 0) {
            if (first) fat_free_chain(first);
            fat_flush();
            return -1;
        }
    }
    if (fat_flush() != 0) return -1;
    return n;
}

int fs_read(const char *path, void *buf, int max) {
    if (!mounted || !path || max < 0 || (max && !buf)) return -1;
    fat_item_t it; int root;
    if (resolve_item(path, &it, &root) != 0 || root || (it.raw[11] & FAT16_ATTR_DIR)) return -1;
    u32 size = rd32(it.raw + 28);
    if (!size || !max) return 0;
    u16 first = rd16(it.raw + 26);
    if (first < 2 || first > max_cluster) return -1;
    return read_file_chain(first, size, buf, (u32)max);
}

void *fs_load(const char *path, int *size) {
    nx_stat st;
    if (fs_stat(path, &st) != 0 || st.is_dir || st.size > 0x7FFFFFFFu) return 0;
    u8 *buf = (u8 *)kmalloc((size_t)st.size + 1);
    if (!buf) return 0;
    if (st.size && fs_read(path, buf, (int)st.size) != (int)st.size) { kfree(buf); return 0; }
    buf[st.size] = 0;
    if (size) *size = (int)st.size;
    return buf;
}

int fs_stat(const char *path, nx_stat *st) {
    if (!mounted || !path || !st) return -1;
    fat_item_t it; int root;
    if (resolve_item(path, &it, &root) != 0) return -1;
    st->is_dir = root ? 1 : ((it.raw[11] & FAT16_ATTR_DIR) != 0);
    st->size = root ? 0 : rd32(it.raw + 28);
    return 0;
}

int fs_list(const char *path, nx_dirent *out, int max) {
    if (!mounted || !path || !out || max < 0) return -1;
    fat_dir_t dir;
    if (!*path || name_equal(path, "/")) { dir.root = 1; dir.cluster = 0; }
    else {
        fat_item_t it; int root;
        if (resolve_item(path, &it, &root) != 0 || (!root && !(it.raw[11] & FAT16_ATTR_DIR))) return -1;
        dir.root = root;
        dir.cluster = root ? 0 : rd16(it.raw + 26);
    }
    u32 cap = dir_capacity(dir);
    char lfn[60]; int active = 0, count = 0;
    memset(lfn, 0, sizeof lfn);
    for (u32 i = 0; i < cap && count < max; i++) {
        u8 e[32];
        if (dir_read_slot(dir, i, e) != 0) return -1;
        if (e[0] == 0) break;
        if (e[0] == 0xE5) { active = 0; memset(lfn, 0, sizeof lfn); continue; }
        if (e[11] == FAT16_ATTR_LFN) { lfn_add(e, lfn, &active); continue; }
        if (e[11] & FAT16_ATTR_VOLUME) { active = 0; continue; }
        char nm[60];
        if (active && lfn[0]) memcpy(nm, lfn, sizeof nm); else short_to_name(e, nm);
        if (name_equal(nm, ".") || name_equal(nm, "..")) { active = 0; continue; }
        strncpy(out[count].name, nm, 59); out[count].name[59] = 0;
        out[count].is_dir = (e[11] & FAT16_ATTR_DIR) != 0;
        out[count].size = rd32(e + 28);
        count++;
        active = 0; memset(lfn, 0, sizeof lfn);
    }
    return count;
}

static int remove_item_recursive(fat_item_t *it, int depth) {
    if (depth >= FAT16_MAX_DEPTH) return -1;
    u16 first = rd16(it->raw + 26);
    if (it->raw[11] & FAT16_ATTR_DIR) {
        fat_dir_t child; child.root = 0; child.cluster = first;
        u32 cap = dir_capacity(child);
        for (u32 i = 0; i < cap; i++) {
            u8 e[32];
            if (dir_read_slot(child, i, e) != 0) return -1;
            if (e[0] == 0) break;
            if (e[0] == 0xE5 || e[11] == FAT16_ATTR_LFN || (e[11] & FAT16_ATTR_VOLUME)) continue;
            char nm[60]; short_to_name(e, nm);
            if (name_equal(nm, ".") || name_equal(nm, "..")) continue;
            fat_item_t sub; memcpy(sub.raw, e, 32); strncpy(sub.name, nm, 59); sub.name[59] = 0;
            sub.slot = i; sub.parent = child;
            if (remove_item_recursive(&sub, depth + 1) != 0) return -1;
        }
    }
    if (first) fat_free_chain(first);
    return dir_delete_entry(it->parent, it->slot);
}

int fs_remove(const char *path) {
    if (!mounted || !path) return -1;
    fat_item_t it; int root;
    if (resolve_item(path, &it, &root) != 0 || root) return -1;
    if (remove_item_recursive(&it, 0) != 0) return -1;
    return fat_flush();
}

static int dir_is_inside(fat_dir_t dir, u16 target) {
    if (dir.root) return 0;
    u16 c = dir.cluster;
    for (u32 depth = 0; depth < FAT16_MAX_DEPTH && c >= 2 && c <= max_cluster; depth++) {
        if (c == target) return 1;
        fat_dir_t d; d.root = 0; d.cluster = c;
        u8 dotdot[32];
        if (dir_read_slot(d, 1, dotdot) != 0) return 0;
        u16 parent = rd16(dotdot + 26);
        if (!parent) return 0;
        c = parent;
    }
    return 0;
}

int fs_rename(const char *from, const char *to) {
    if (!mounted || !from || !to) return -1;
    fat_item_t it; int root;
    if (resolve_item(from, &it, &root) != 0 || root) return -1;
    fat_dir_t new_parent; char new_leaf[60];
    if (path_parent(to, &new_parent, new_leaf) != 0) return -1;
    fat_item_t existing;
    if (dir_find(new_parent, new_leaf, &existing) == 0) {
        if (new_parent.root == it.parent.root && new_parent.cluster == it.parent.cluster && existing.slot == it.slot) return 0;
        return -1;
    }
    u16 cluster = rd16(it.raw + 26);
    if ((it.raw[11] & FAT16_ATTR_DIR) && dir_is_inside(new_parent, cluster)) return -1;
    u32 new_slot;
    if (dir_create_entry(new_parent, new_leaf, it.raw[11], cluster, rd32(it.raw + 28), &new_slot) != 0) return -1;
    if (it.raw[11] & FAT16_ATTR_DIR) {
        fat_dir_t moved; moved.root = 0; moved.cluster = cluster;
        u8 dotdot[32];
        if (dir_read_slot(moved, 1, dotdot) != 0) return -1;
        wr16(dotdot + 26, new_parent.root ? 0 : new_parent.cluster);
        if (dir_write_slot(moved, 1, dotdot) != 0) return -1;
    }
    if (dir_delete_entry(it.parent, it.slot) != 0) return -1;
    (void)new_slot;
    return fat_flush();
}

static int fat16_mount(void) {
    u8 mbr[512] __attribute__((aligned(2)));
    if (disk_read(0, 1, mbr) != 0 || mbr[510] != 0x55 || mbr[511] != 0xAA) return -1;
    part_lba = part_sectors = 0;
    for (int i = 0; i < 4; i++) {
        const u8 *p = mbr + 446 + i * 16;
        if (p[4] == FAT16_TYPE || p[4] == 0x0E || p[4] == 0x04) {
            part_lba = rd32(p + 8); part_sectors = rd32(p + 12);
            if (part_lba && part_sectors) break;
        }
    }
    if (!part_lba || !part_sectors || part_lba > disk_sector_count() ||
        part_sectors > disk_sector_count() - part_lba) return -1;
    u8 bpb[512] __attribute__((aligned(2)));
    if (disk_read(part_lba, 1, bpb) != 0 || bpb[510] != 0x55 || bpb[511] != 0xAA) return -1;
    if (rd16(bpb + 11) != 512) return -1;
    sectors_per_cluster = bpb[13];
    u32 reserved = rd16(bpb + 14);
    fat_count = bpb[16];
    root_entries = rd16(bpb + 17);
    u32 total = rd16(bpb + 19); if (!total) total = rd32(bpb + 32);
    fat_sectors = rd16(bpb + 22);
    if (!sectors_per_cluster || (sectors_per_cluster & (sectors_per_cluster - 1)) ||
        sectors_per_cluster > 64 || !reserved || !fat_count || fat_count > 2 ||
        !root_entries || !fat_sectors || total > part_sectors || total < 100) return -1;
    root_sectors = (root_entries * 32 + 511) / 512;
    fat_lba = part_lba + reserved;
    root_lba = fat_lba + fat_count * fat_sectors;
    data_lba = root_lba + root_sectors;
    if (data_lba >= part_lba + total) return -1;
    cluster_count = (part_lba + total - data_lba) / sectors_per_cluster;
    if (cluster_count < 4085 || cluster_count >= 65525) return -1; /* FAT16, nao FAT12/32 */
    max_cluster = cluster_count + 1;
    fat_cache_bytes = fat_sectors * 512;
    if (fat_cache_bytes < (max_cluster + 1) * 2 || fat_sectors > FAT16_MAX_FAT_SECT) return -1;
    fat_cache = (u8 *)kmalloc(fat_cache_bytes);
    if (!fat_cache) return -1;
    if (disk_read(fat_lba, fat_sectors, fat_cache) != 0) { kfree(fat_cache); fat_cache = 0; return -1; }
    if (rd16(fat_cache) < 0xFFF8 || rd16(fat_cache + 2) < 0xFFF8) { kfree(fat_cache); fat_cache = 0; return -1; }
    sector_cache_lba = 0xFFFFFFFF;
    mounted = 1;
    kprintf("[fs] FAT16 montado: LBA %u, %u MiB, cluster=%u setores\n",
            part_lba, (total >> 11), sectors_per_cluster);
    return 0;
}

static void import_initrd(void) {
    const u8 *p = (const u8 *)__initrd_start;
    const u8 *end = (const u8 *)__initrd_end;
    if (p + 8 > end || memcmp(p, "NXAR", 4) != 0) {
        kprintf("[fs] initrd NXAR ausente/invalido\n"); return;
    }
    u32 count = rd32(p + 4); p += 8;
    for (u32 i = 0; i < count; i++) {
        if (p + 8 > end) break;
        u16 pl = rd16(p); u8 isdir = p[2]; u32 size = rd32(p + 4);
        p += 8;
        if (!pl || pl > 150 || p + ((pl + 3u) & ~3u) > end) break;
        char path[160]; memcpy(path, p, pl); path[pl] = 0;
        p += (pl + 3u) & ~3u;
        if (isdir) {
            nx_stat st;
            if (fs_stat(path, &st) != 0) fs_mkdir(path);
        } else {
            u32 padded = (size + 3u) & ~3u;
            if (padded < size || (u64)(end - p) < padded || size > 0x7FFFFFFFu) break;
            nx_stat st;
            if (fs_stat(path, &st) != 0) fs_write(path, p, (int)size);
            p += padded;
        }
    }
}

void fs_init(void) {
    mounted = 0; fat_cache = 0; sector_cache_lba = 0xFFFFFFFF;
    if (disk_init() != 0) {
        kprintf("[fs] armazenamento ATA indisponivel; apps persistentes desativados\n");
        return;
    }
    if (fat16_mount() != 0) {
        kprintf("[fs] nenhuma particao FAT16 valida encontrada\n");
        return;
    }
    nx_stat st;
    if (fs_stat("/Apps", &st) != 0) fs_mkdir("/Apps");
    if (fs_stat("/Documentos", &st) != 0) fs_mkdir("/Documentos");
    if (fs_stat("/Imagens", &st) != 0) fs_mkdir("/Imagens");
    import_initrd();
    kprintf("[fs] armazenamento persistente pronto\n");
}
