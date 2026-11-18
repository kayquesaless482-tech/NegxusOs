/* NegxusOS - driver ATA PIO para discos IDE / SATA em modo de compatibilidade.
 * LBA28, canal primario, dispositivo master. Cada setor tem 512 bytes.
 */
#include "kernel.h"

#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_SR_BSY     0x80
#define ATA_SR_DF      0x20
#define ATA_SR_DRQ     0x08
#define ATA_SR_ERR     0x01
#define ATA_CMD_IDENT  0xEC
#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_FLUSH  0xE7
#define ATA_LBA28_MAX  0x0FFFFFFFUL

static u32 g_sectors;
static int g_ready;

static void ata_delay400(void) {
    (void)inb(ATA_STATUS); (void)inb(ATA_STATUS);
    (void)inb(ATA_STATUS); (void)inb(ATA_STATUS);
}

static int ata_wait(int need_drq) {
    for (u32 i = 0; i < 1000000; i++) {
        u8 s = inb(ATA_STATUS);
        if (s == 0 || s == 0xFF) return -1;
        if (s & ATA_SR_BSY) continue;
        if (s & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (!need_drq || (s & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static void ata_select(u32 lba) {
    outb(ATA_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay400();
}

int disk_init(void) {
    u16 identify[256];
    g_ready = 0;
    g_sectors = 0;

    outb(ATA_DRIVE, 0xA0);          /* canal primario, master */
    ata_delay400();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENT);

    u8 status = inb(ATA_STATUS);
    if (status == 0 || status == 0xFF) {
        kprintf("[disk] nenhum dispositivo ATA no canal primario/master\n");
        return -1;
    }
    for (u32 i = 0; i < 1000000; i++) {
        status = inb(ATA_STATUS);
        if (!(status & ATA_SR_BSY)) break;
    }
    if (status == 0 || status == 0xFF || (status & ATA_SR_ERR)) {
        kprintf("[disk] IDENTIFY falhou\n");
        return -1;
    }
    if (inb(ATA_LBA1) || inb(ATA_LBA2)) {
        kprintf("[disk] dispositivo nao e ATA (possivel ATAPI)\n");
        return -1;
    }
    if (ata_wait(1) != 0) {
        kprintf("[disk] IDENTIFY sem dados\n");
        return -1;
    }
    for (int i = 0; i < 256; i++) identify[i] = inw(ATA_DATA);

    g_sectors = ((u32)identify[61] << 16) | identify[60];
    if (!g_sectors) {
        kprintf("[disk] dispositivo sem capacidade LBA28\n");
        return -1;
    }
    if (g_sectors > ATA_LBA28_MAX + 1) g_sectors = ATA_LBA28_MAX + 1;
    g_ready = 1;
    kprintf("[disk] ATA PIO pronto: %u setores (%u MiB)\n",
            g_sectors, (g_sectors >> 11));
    return 0;
}

int disk_read(u32 lba, u32 count, void *buffer) {
    if (!g_ready || !buffer || !count || lba > ATA_LBA28_MAX ||
        count - 1 > ATA_LBA28_MAX - lba || lba + count > g_sectors) return -1;

    u8 *dst = (u8 *)buffer;
    while (count--) {
        if (ata_wait(0) != 0) return -1;
        ata_select(lba);
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA0, (u8)lba);
        outb(ATA_LBA1, (u8)(lba >> 8));
        outb(ATA_LBA2, (u8)(lba >> 16));
        outb(ATA_COMMAND, ATA_CMD_READ);
        if (ata_wait(1) != 0) return -1;
        for (int i = 0; i < 256; i++) {
            u16 v = inw(ATA_DATA);
            dst[i * 2] = (u8)v;
            dst[i * 2 + 1] = (u8)(v >> 8);
        }
        dst += 512;
        lba++;
    }
    return 0;
}

int disk_write(u32 lba, u32 count, const void *buffer) {
    if (!g_ready || !buffer || !count || lba > ATA_LBA28_MAX ||
        count - 1 > ATA_LBA28_MAX - lba || lba + count > g_sectors) return -1;

    const u8 *src = (const u8 *)buffer;
    while (count--) {
        if (ata_wait(0) != 0) return -1;
        ata_select(lba);
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA0, (u8)lba);
        outb(ATA_LBA1, (u8)(lba >> 8));
        outb(ATA_LBA2, (u8)(lba >> 16));
        outb(ATA_COMMAND, ATA_CMD_WRITE);
        if (ata_wait(1) != 0) return -1;
        for (int i = 0; i < 256; i++) {
            u16 v = (u16)src[i * 2] | ((u16)src[i * 2 + 1] << 8);
            outw(ATA_DATA, v);
        }
        if (ata_wait(0) != 0) return -1;
        src += 512;
        lba++;
    }
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    return ata_wait(0);
}

u32 disk_sector_count(void) { return g_sectors; }
int disk_is_ready(void) { return g_ready; }
