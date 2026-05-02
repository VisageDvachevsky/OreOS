#include "kernel.h"

typedef struct {
    const char *name;
    uint64_t block_size;
    uint64_t block_count;
    int (*read)(uint64_t lba, void *buffer, uint32_t blocks);
} BlockDevice;

static BlockDevice ata_secondary;
static BlockDevice fwcfg_disk;
static BlockDevice *root_block;
static uint16_t fwcfg_disk_selector;

static void io_wait(void) {
    outb(0x80, 0);
}

static int ata_wait(uint8_t mask, uint8_t value) {
    for (uint32_t i = 0; i < 1000000; ++i) {
        uint8_t status = inb(0x1F7);
        if (status & 0x01) return -1;
        if ((status & mask) == value) return 0;
    }
    return -1;
}

static int ata_identify_slave(uint64_t *sectors) {
    outb(0x1F6, 0xB0);
    io_wait();
    outb(0x1F2, 0);
    outb(0x1F3, 0);
    outb(0x1F4, 0);
    outb(0x1F5, 0);
    outb(0x1F7, 0xEC);
    io_wait();

    uint8_t status = inb(0x1F7);
    if (!status) return -1;
    if (ata_wait(0x88, 0x08) < 0) return -1;

    uint16_t identify[256];
    for (uint32_t i = 0; i < 256; ++i) identify[i] = inw(0x1F0);
    uint32_t lba28 = ((uint32_t)identify[61] << 16) | identify[60];
    *sectors = lba28;
    return lba28 ? 0 : -1;
}

static int ata_read_slave(uint64_t lba, void *buffer, uint32_t blocks) {
    if (!buffer || !blocks || lba + blocks > 0x0FFFFFFFULL) return -1;
    uint8_t *out = buffer;
    for (uint32_t b = 0; b < blocks; ++b) {
        uint32_t cur = (uint32_t)(lba + b);
        if (ata_wait(0x80, 0) < 0) return -1;
        outb(0x1F6, (uint8_t)(0xE0 | 0x10 | ((cur >> 24) & 0x0F)));
        outb(0x1F2, 1);
        outb(0x1F3, (uint8_t)(cur & 0xFF));
        outb(0x1F4, (uint8_t)((cur >> 8) & 0xFF));
        outb(0x1F5, (uint8_t)((cur >> 16) & 0xFF));
        outb(0x1F7, 0x20);
        if (ata_wait(0x88, 0x08) < 0) return -1;
        for (uint32_t i = 0; i < 256; ++i) {
            uint16_t word = inw(0x1F0);
            out[i * 2] = (uint8_t)(word & 0xFF);
            out[i * 2 + 1] = (uint8_t)(word >> 8);
        }
        out += 512;
    }
    return 0;
}

static uint8_t fwcfg_read8(void) {
    return inb(0x511);
}

static uint16_t fwcfg_read_be16(void) {
    uint16_t a = fwcfg_read8();
    uint16_t b = fwcfg_read8();
    return (uint16_t)((a << 8) | b);
}

static uint32_t fwcfg_read_be32(void) {
    uint32_t a = fwcfg_read8();
    uint32_t b = fwcfg_read8();
    uint32_t c = fwcfg_read8();
    uint32_t d = fwcfg_read8();
    return (a << 24) | (b << 16) | (c << 8) | d;
}

static void fwcfg_select(uint16_t selector) {
    outw(0x510, selector);
}

static int name_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int fwcfg_find_disk(uint32_t *size, uint16_t *selector) {
    fwcfg_select(0x0019);
    uint32_t count = fwcfg_read_be32();
    for (uint32_t i = 0; i < count && i < 128; ++i) {
        uint32_t file_size = fwcfg_read_be32();
        uint16_t file_selector = fwcfg_read_be16();
        (void)fwcfg_read_be16();
        char name[56];
        for (uint32_t j = 0; j < sizeof(name); ++j) name[j] = (char)fwcfg_read8();
        name[55] = 0;
        if (name_eq(name, "opt/ore/disk")) {
            *size = file_size;
            *selector = file_selector;
            return 0;
        }
    }
    return -1;
}

static int fwcfg_read_disk(uint64_t lba, void *buffer, uint32_t blocks) {
    if (!buffer || !blocks) return -1;
    uint64_t offset = lba * 512;
    uint64_t bytes = (uint64_t)blocks * 512;
    fwcfg_select(fwcfg_disk_selector);
    for (uint64_t i = 0; i < offset; ++i) (void)fwcfg_read8();
    uint8_t *out = buffer;
    for (uint64_t i = 0; i < bytes; ++i) out[i] = fwcfg_read8();
    return 0;
}

void block_init(void) {
    root_block = 0;
    uint64_t sectors = 0;
    uint32_t fw_size = 0;
    uint16_t fw_selector = 0;
    if (fwcfg_find_disk(&fw_size, &fw_selector) == 0 && fw_size >= 512) {
        fwcfg_disk_selector = fw_selector;
        fwcfg_disk.name = "fwcfg-ore-disk";
        fwcfg_disk.block_size = 512;
        fwcfg_disk.block_count = fw_size / 512;
        fwcfg_disk.read = fwcfg_read_disk;
        root_block = &fwcfg_disk;
        kprintf("block: root device fw_cfg opt/ore/disk blocks 0x%lx\n", fwcfg_disk.block_count);
        return;
    }
    if (ata_identify_slave(&sectors) == 0) {
        ata_secondary.name = "ata1";
        ata_secondary.block_size = 512;
        ata_secondary.block_count = sectors;
        ata_secondary.read = ata_read_slave;
        root_block = &ata_secondary;
        kprintf("block: root device ata1 blocks 0x%lx size 0x200\n", sectors);
    } else {
        kprintf("block: layer ready (no persistent device attached)\n");
    }
}

int block_root_read(uint64_t lba, void *buffer, uint32_t blocks) {
    if (!root_block || !root_block->read || root_block->block_size != 512) return -1;
    if (lba + blocks > root_block->block_count) return -1;
    return root_block->read(lba, buffer, blocks);
}

uint64_t block_root_count(void) {
    return root_block ? root_block->block_count : 0;
}
