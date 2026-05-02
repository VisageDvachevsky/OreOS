#include "kernel.h"

#define OKMOD_MAX_SIZE (128 * 1024ULL)
#define OKMOD_PAGE_SIZE 4096ULL
#define OKMOD_MAX_MODULES 16

typedef int (*OkModInit)(OreKernelApi *api);
typedef int (*OkModCommand)(char *out, uint64_t out_len);

static uint32_t loaded_modules;
static OreModuleInfo module_table[OKMOD_MAX_MODULES];
static OkModCommand module_commands[OKMOD_MAX_MODULES];

static void okmod_log(const char *s) {
    if (s) kprintf("%s", s);
}

static OreKernelApi kernel_api = {
    .log = okmod_log,
    .alloc = kmalloc,
    .free = kfree,
    .vfs_stat = vfs_stat,
    .vfs_read_file_at = vfs_read_file_at,
    .vfs_readdir_path = vfs_readdir_path,
};

static void mem_copy_okmod(void *dst, const void *src, uint64_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
}

static void mem_zero_okmod(void *dst, uint64_t n) {
    uint8_t *d = dst;
    for (uint64_t i = 0; i < n; ++i) d[i] = 0;
}

static uint64_t str_len_okmod(const char *s) {
    uint64_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static int str_eq_okmod(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int has_suffix_okmod(const char *s, const char *suffix) {
    uint64_t sl = str_len_okmod(s);
    uint64_t tl = str_len_okmod(suffix);
    if (sl < tl) return 0;
    return str_eq_okmod(s + sl - tl, suffix);
}

static void copy_str_okmod(char *dst, const char *src, uint64_t cap) {
    uint64_t i = 0;
    if (!cap) return;
    for (; i + 1 < cap && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static void module_name_from_path(const char *path, char out[32]) {
    const char *base = path;
    for (uint64_t i = 0; path[i]; ++i) {
        if (path[i] == '/') base = path + i + 1;
    }
    uint64_t i = 0;
    for (; i + 1 < 32 && base[i] && base[i] != '.'; ++i) out[i] = base[i];
    out[i] = 0;
}

static int okmod_load_one(const char *path) {
    if (loaded_modules >= OKMOD_MAX_MODULES) {
        kprintf("okmod: reject %s module table full\n", path);
        return -1;
    }
    VfsStat st;
    if (vfs_stat(path, &st) < 0) return -1;
    if (st.type != ORE_VFS_FILE || st.size < sizeof(OreKModHeader) || st.size > OKMOD_MAX_SIZE) {
        kprintf("okmod: reject %s invalid size/type\n", path);
        return -1;
    }

    uint8_t *file = kmalloc(st.size);
    if (!file) return -1;
    int64_t n = vfs_read_file(path, file, st.size);
    if (n != (int64_t)st.size) {
        kfree(file);
        return -1;
    }

    OreKModHeader *hdr = (OreKModHeader *)file;
    OreModuleInfo *slot = &module_table[loaded_modules];
    mem_zero_okmod(slot, sizeof(*slot));
    copy_str_okmod(slot->path, path, sizeof(slot->path));
    if (hdr->header_size >= sizeof(OreKModHeader) && hdr->name[0]) copy_str_okmod(slot->name, hdr->name, sizeof(slot->name));
    else module_name_from_path(path, slot->name);
    slot->state = 2;
    slot->abi_version = hdr->kernel_abi_version;
    if (hdr->magic != ORE_KMOD_MAGIC ||
        hdr->version != ORE_KMOD_VERSION ||
        hdr->kernel_abi_version != ORE_KERNEL_ABI_VERSION ||
        hdr->header_size < sizeof(OreKModHeader) ||
        hdr->reloc_count || hdr->import_count || hdr->export_count) {
        slot->state = 4;
        kprintf("okmod: reject %s bad header\n", path);
        kfree(file);
        return -1;
    }

    uint64_t text_off = hdr->header_size;
    uint64_t data_off = text_off + hdr->text_size;
    uint64_t total = hdr->text_size + hdr->data_size + hdr->bss_size;
    if (!hdr->text_size || hdr->entry >= hdr->text_size || data_off + hdr->data_size > st.size || total > OKMOD_MAX_SIZE) {
        slot->state = 4;
        kprintf("okmod: reject %s bad layout\n", path);
        kfree(file);
        return -1;
    }

    uint32_t pages = (uint32_t)((total + OKMOD_PAGE_SIZE - 1) / OKMOD_PAGE_SIZE);
    uint8_t *mem = pmm_alloc_pages(pages);
    if (!mem) {
        kfree(file);
        return -1;
    }
    mem_zero_okmod(mem, pages * OKMOD_PAGE_SIZE);
    mem_copy_okmod(mem, file + text_off, hdr->text_size);
    mem_copy_okmod(mem + hdr->text_size, file + data_off, hdr->data_size);
    mem_zero_okmod(mem + hdr->text_size + hdr->data_size, hdr->bss_size);
    if (vmm_protect((uint64_t)(uintptr_t)mem, hdr->text_size, VMM_PRESENT) < 0) {
        slot->state = 4;
        kprintf("okmod: reject %s text protect failed\n", path);
        pmm_free_pages(mem, pages);
        kfree(file);
        return -1;
    }
    if (pages * OKMOD_PAGE_SIZE > hdr->text_size) {
        uint64_t rw_base = (uint64_t)(uintptr_t)(mem + hdr->text_size);
        uint64_t rw_size = pages * OKMOD_PAGE_SIZE - hdr->text_size;
        (void)vmm_protect(rw_base, rw_size, VMM_PRESENT | VMM_WRITE | VMM_NX);
    }

    OkModInit init = (OkModInit)(uintptr_t)(mem + hdr->entry);
    int rc = init(&kernel_api);
    slot->init_rc = rc;
    slot->text_base = (uint64_t)(uintptr_t)mem;
    slot->text_size = hdr->text_size;
    slot->data_size = hdr->data_size;
    if (hdr->command_entry != 0xffffffffffffffffULL && hdr->command_entry < hdr->text_size) {
        slot->has_command = 1;
        module_commands[loaded_modules] = (OkModCommand)(uintptr_t)(mem + hdr->command_entry);
    }
    if (rc < 0) {
        slot->state = 4;
        kprintf("okmod: %s init failed %u\n", path, (unsigned)rc);
        pmm_free_pages(mem, pages);
        kfree(file);
        return -1;
    }

    slot->state = 3;
    loaded_modules++;
    kprintf("okmod: loaded %s (%s) text 0x%lx data 0x%lx rc %u\n", path, slot->name, hdr->text_size, hdr->data_size, (unsigned)rc);
    kfree(file);
    return 0;
}

void okmod_init(void) {
    loaded_modules = 0;
    mem_zero_okmod(module_table, sizeof(module_table));
    mem_zero_okmod(module_commands, sizeof(module_commands));
    uint32_t attempted = 0;
    VfsStat st;
    for (uint32_t i = 0; vfs_readdir_path("/disk/kernel", i, &st) == 0; ++i) {
        if (st.type != ORE_VFS_FILE || !has_suffix_okmod(st.name, ".okmod")) continue;
        char path[160];
        const char *prefix = "/disk/kernel/";
        uint64_t pos = 0;
        for (uint64_t j = 0; prefix[j] && pos + 1 < sizeof(path); ++j) path[pos++] = prefix[j];
        for (uint64_t j = 0; st.name[j] && pos + 1 < sizeof(path); ++j) path[pos++] = st.name[j];
        path[pos] = 0;
        attempted++;
        (void)okmod_load_one(path);
    }
    if (!attempted || !loaded_modules) {
        kprintf("okmod: no trusted modules loaded\n");
    } else {
        kprintf("okmod: modules online %u\n", loaded_modules);
    }
}

uint32_t okmod_count(void) {
    return loaded_modules;
}

int okmod_info(uint32_t index, OreModuleInfo *info) {
    if (!info || index >= loaded_modules) return -1;
    mem_copy_okmod(info, &module_table[index], sizeof(*info));
    return 0;
}

int okmod_run_command(const char *name, char *out, uint64_t out_len) {
    if (!name || !name[0]) return -1;
    if (out && out_len) out[0] = 0;
    for (uint32_t i = 0; i < loaded_modules; ++i) {
        if (module_table[i].has_command && str_eq_okmod(module_table[i].name, name) && module_commands[i]) {
            return module_commands[i](out, out_len);
        }
    }
    return -1;
}
