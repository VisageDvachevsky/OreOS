#include "uefi.h"
#include "bootinfo.h"

#define EI_NIDENT 16
#define PT_LOAD 1

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef void (__attribute__((sysv_abi)) *KernelEntry)(const BootInfo *);

static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;

static const EFI_GUID simple_fs_guid = {0x0964e5b22, 0x6459, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID loaded_image_guid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID gop_guid = {0x9042a9de, 0x23dc, 0x4a38, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static const EFI_GUID acpi20_guid = {0x8868e871, 0xe4f1, 0x11d3, {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
static const EFI_GUID acpi10_guid = {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};

typedef struct { uint32_t revision; EFI_HANDLE parent_handle; EFI_SYSTEM_TABLE *system_table; EFI_HANDLE device_handle; } EFI_LOADED_IMAGE_PROTOCOL;

static void *memset_local(void *ptr, int value, UINTN size) {
    unsigned char *p = ptr;
    while (size--) *p++ = (unsigned char)value;
    return ptr;
}

static void *memcpy_local(void *dst, const void *src, UINTN size) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (size--) *d++ = *s++;
    return dst;
}

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (UINTN i = 0; i < sizeof(EFI_GUID); ++i) if (x[i] != y[i]) return 0;
    return 1;
}

static void puts16(CHAR16 *s) {
    g_st->con_out->output_string(g_st->con_out, s);
}

static void io_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t io_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    io_out8(0x3f8 + 1, 0x00);
    io_out8(0x3f8 + 3, 0x80);
    io_out8(0x3f8 + 0, 0x03);
    io_out8(0x3f8 + 1, 0x00);
    io_out8(0x3f8 + 3, 0x03);
    io_out8(0x3f8 + 2, 0xc7);
    io_out8(0x3f8 + 4, 0x0b);
}

static void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    while ((io_in8(0x3f8 + 5) & 0x20) == 0) {}
    io_out8(0x3f8, (uint8_t)c);
}

static void serial_write(const char *s) {
    while (*s) serial_putc(*s++);
}

static EFI_STATUS read_file(EFI_HANDLE image, CHAR16 *path, void **buffer, UINTN *size) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *file = 0;
    EFI_STATUS st;

    st = g_bs->open_protocol(image, (EFI_GUID *)&loaded_image_guid, (void **)&loaded, image, 0, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(st)) return st;
    st = g_bs->open_protocol(loaded->device_handle, (EFI_GUID *)&simple_fs_guid, (void **)&fs, image, 0, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(st)) return st;
    st = fs->open_volume(fs, &root);
    if (EFI_ERROR(st)) return st;
    st = root->open(root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) return st;

    UINTN cap = 16 * 1024 * 1024;
    st = g_bs->allocate_pool(EFI_LOADER_DATA, cap, buffer);
    if (EFI_ERROR(st)) return st;
    *size = cap;
    st = file->read(file, size, *buffer);
    file->close(file);
    return st;
}

static EFI_STATUS load_kernel(void *elf, UINTN elf_size, uint64_t *entry, uint64_t *base, uint64_t *total_size) {
    (void)elf_size;
    Elf64_Ehdr *eh = (Elf64_Ehdr *)elf;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') return EFI_LOAD_ERROR;
    *entry = eh->e_entry;
    uint64_t min_addr = UINT64_MAX;
    uint64_t max_addr = 0;
    Elf64_Phdr *ph = (Elf64_Phdr *)((uint8_t *)elf + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        uint64_t addr = ph[i].p_paddr ? ph[i].p_paddr : ph[i].p_vaddr;
        if (addr < min_addr) min_addr = addr;
        if (addr + ph[i].p_memsz > max_addr) max_addr = addr + ph[i].p_memsz;
    }
    if (min_addr == UINT64_MAX || max_addr <= min_addr) return EFI_LOAD_ERROR;

    uint64_t aligned_base = min_addr & ~0xfffULL;
    uint64_t aligned_end = (max_addr + 0xfffULL) & ~0xfffULL;
    EFI_PHYSICAL_ADDRESS allocation = aligned_base;
    UINTN pages = (aligned_end - aligned_base) / 0x1000;
    EFI_STATUS st = g_bs->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, pages, &allocation);
    if (EFI_ERROR(st)) return st;
    memset_local((void *)(uintptr_t)aligned_base, 0, aligned_end - aligned_base);

    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        uint64_t addr = ph[i].p_paddr ? ph[i].p_paddr : ph[i].p_vaddr;
        memcpy_local((void *)(uintptr_t)addr, (uint8_t *)elf + ph[i].p_offset, ph[i].p_filesz);
    }
    *base = aligned_base;
    *total_size = aligned_end - aligned_base;
    return EFI_SUCCESS;
}

static void fill_framebuffer(BootInfo *bi) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    if (EFI_ERROR(g_bs->locate_protocol((EFI_GUID *)&gop_guid, 0, (void **)&gop)) || !gop || !gop->mode) return;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->mode->info;
    bi->framebuffer.base = gop->mode->frame_buffer_base;
    bi->framebuffer.width = info->horizontal_resolution;
    bi->framebuffer.height = info->vertical_resolution;
    bi->framebuffer.pixels_per_scanline = info->pixels_per_scanline;
    bi->framebuffer.pixel_format = info->pixel_format;
}

static void fill_acpi(BootInfo *bi) {
    for (UINTN i = 0; i < g_st->number_of_table_entries; ++i) {
        EFI_CONFIGURATION_TABLE *t = &g_st->configuration_table[i];
        if (guid_eq(&t->vendor_guid, &acpi20_guid) || guid_eq(&t->vendor_guid, &acpi10_guid)) {
            bi->rsdp = (uint64_t)(uintptr_t)t->vendor_table;
            return;
        }
    }
}

EFI_STATUS __attribute__((ms_abi)) efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table) {
    g_st = system_table;
    g_bs = system_table->boot_services;
    serial_init();
    serial_write("Ore loader: serial online\n");
    puts16(L"Ore loader: loading kernel.elf\r\n");

    void *kernel_file = 0;
    UINTN kernel_file_size = 0;
    EFI_STATUS st = read_file(image, L"\\kernel.elf", &kernel_file, &kernel_file_size);
    if (EFI_ERROR(st)) { puts16(L"Ore loader: cannot read kernel.elf\r\n"); return st; }

    uint64_t entry = 0, kernel_base = 0, kernel_size = 0;
    st = load_kernel(kernel_file, kernel_file_size, &entry, &kernel_base, &kernel_size);
    if (EFI_ERROR(st)) { puts16(L"Ore loader: ELF load failed\r\n"); return st; }
    serial_write("Ore loader: kernel ELF loaded\n");

    BootInfo *bi = 0;
    st = g_bs->allocate_pool(EFI_LOADER_DATA, sizeof(BootInfo), (void **)&bi);
    if (EFI_ERROR(st)) return st;
    memset_local(bi, 0, sizeof(BootInfo));
    bi->magic = ORE_BOOTINFO_MAGIC;
    bi->kernel_base = kernel_base;
    bi->kernel_size = kernel_size;
    bi->cmdline[0] = 0;
    void *initramfs = 0;
    UINTN initramfs_size = 0;
    st = read_file(image, L"\\initramfs.tar", &initramfs, &initramfs_size);
    if (!EFI_ERROR(st)) {
        bi->initramfs_base = (uint64_t)(uintptr_t)initramfs;
        bi->initramfs_size = initramfs_size;
        serial_write("Ore loader: initramfs loaded\n");
    } else {
        bi->initramfs_base = 0;
        bi->initramfs_size = 0;
        serial_write("Ore loader: no initramfs\n");
    }
    fill_framebuffer(bi);
    fill_acpi(bi);

    UINTN mmap_size = 0, map_key = 0, desc_size = 0;
    uint32_t desc_ver = 0;
    g_bs->get_memory_map(&mmap_size, 0, &map_key, &desc_size, &desc_ver);
    mmap_size += desc_size * 8;
    EFI_MEMORY_DESCRIPTOR *mmap = 0;
    st = g_bs->allocate_pool(EFI_LOADER_DATA, mmap_size, (void **)&mmap);
    if (EFI_ERROR(st)) return st;
    st = g_bs->get_memory_map(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(st)) return st;

    UINTN entries = mmap_size / desc_size;
    bi->memory_map_entries = entries > ORE_MEMMAP_MAX ? ORE_MEMMAP_MAX : entries;
    bi->memory_map_descriptor_size = sizeof(OreMemoryDescriptor);
    for (UINTN i = 0; i < bi->memory_map_entries; ++i) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)mmap + i * desc_size);
        bi->memory_map[i].type = d->type;
        bi->memory_map[i].physical_start = d->physical_start;
        bi->memory_map[i].virtual_start = d->virtual_start;
        bi->memory_map[i].number_of_pages = d->number_of_pages;
        bi->memory_map[i].attribute = d->attribute;
    }

    st = g_bs->exit_boot_services(image, map_key);
    if (EFI_ERROR(st)) {
        st = g_bs->get_memory_map(&mmap_size, mmap, &map_key, &desc_size, &desc_ver);
        if (!EFI_ERROR(st)) st = g_bs->exit_boot_services(image, map_key);
        if (EFI_ERROR(st)) return st;
    }

    serial_write("Ore loader: jumping to kernel\n");
    ((KernelEntry)(uintptr_t)entry)(bi);
    for (;;) __asm__ volatile("hlt");
}
