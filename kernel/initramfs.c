#include "kernel.h"

#define TAR_BLOCK 512U

typedef struct {
    const char *name;
    const uint8_t *data;
    uint64_t size;
    uint32_t type;
} InitramfsFile;

static uint8_t *archive_base;
static uint64_t archive_size;
static InitramfsFile files[64];
static uint32_t file_count;

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static uint64_t parse_octal(const char *s, uint32_t n) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (s[i] >= '0' && s[i] <= '7') value = (value << 3) + (uint64_t)(s[i] - '0');
    }
    return value;
}

static int block_empty(const uint8_t *p) {
    for (uint32_t i = 0; i < TAR_BLOCK; ++i) {
        if (p[i]) return 0;
    }
    return 1;
}

void initramfs_init(const BootInfo *boot_info) {
    archive_base = (uint8_t *)(uintptr_t)boot_info->initramfs_base;
    archive_size = boot_info->initramfs_size;
    file_count = 0;
    if (!archive_base || !archive_size) {
        kprintf("initramfs: not present\n");
        return;
    }

    uint64_t off = 0;
    while (off + TAR_BLOCK <= archive_size && file_count < 64) {
        uint8_t *hdr = archive_base + off;
        if (block_empty(hdr)) break;
        const char *name = (const char *)hdr;
        uint64_t size = parse_octal((const char *)hdr + 124, 12);
        char typeflag = (char)hdr[156];
        off += TAR_BLOCK;
        if (name[0] && typeflag != '5') {
            files[file_count].name = name;
            files[file_count].data = archive_base + off;
            files[file_count].size = size;
            files[file_count].type = 1;
            file_count++;
        }
        off += ((size + TAR_BLOCK - 1) / TAR_BLOCK) * TAR_BLOCK;
    }
    kprintf("initramfs: base 0x%lx size 0x%lx files %u\n",
            (uint64_t)(uintptr_t)archive_base, archive_size, file_count);
}

uint32_t initramfs_count(void) {
    return file_count;
}

const InitramfsFile *initramfs_file(uint32_t index) {
    if (index >= file_count) return 0;
    return &files[index];
}

const InitramfsFile *initramfs_lookup_raw(const char *path) {
    const char *p = path;
    if (p[0] == '/') p++;
    for (uint32_t i = 0; i < file_count; ++i) {
        if (str_eq(files[i].name, p)) return &files[i];
    }
    return 0;
}
