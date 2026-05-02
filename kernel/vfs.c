#include "kernel.h"

typedef struct {
    const char *name;
    const uint8_t *data;
    uint64_t size;
    uint32_t type;
} InitramfsFileView;

extern uint32_t initramfs_count(void);
extern const InitramfsFileView *initramfs_file(uint32_t index);
extern const InitramfsFileView *initramfs_lookup_raw(const char *path);

static int mounted;
static int disk_mounted;

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t count;
} OreFsHeader;

typedef struct {
    char name[96];
    uint64_t offset;
    uint64_t size;
    uint32_t type;
    uint32_t reserved;
    uint8_t pad[8];
} OreFsEntry;

#define OREFS_MAX_ENTRIES 128

static OreFsEntry disk_entries[OREFS_MAX_ENTRIES];
static uint32_t disk_entry_count;

static void mem_copy_local(void *dst, const void *src, uint64_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < size; ++i) d[i] = s[i];
}

static void copy_name(char dst[128], const char *src) {
    uint32_t i = 0;
    for (; i < 127 && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static void copy_name_len(char dst[128], const char *src, uint64_t len) {
    uint64_t i = 0;
    for (; i < 127 && i < len && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static int str_eq_local(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int str_prefix_eq(const char *a, const char *b, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static uint64_t str_len_local(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

static int starts_with_dir(const char *name, const char *dir, uint64_t dir_len) {
    if (!dir_len) return 1;
    for (uint64_t i = 0; i < dir_len; ++i) {
        if (name[i] != dir[i]) return 0;
    }
    return name[dir_len] == '/';
}

static int path_has_children(const char *path) {
    const char *p = path;
    if (p[0] == '/') p++;
    uint64_t len = str_len_local(p);
    for (uint32_t i = 0; i < initramfs_count(); ++i) {
        const InitramfsFileView *file = initramfs_file(i);
        if (file && starts_with_dir(file->name, p, len)) return 1;
    }
    return 0;
}

static int direct_child(const char *dir, const InitramfsFileView *file, char name[128], uint32_t *type, uint64_t *size) {
    uint64_t dir_len = str_len_local(dir);
    if (!starts_with_dir(file->name, dir, dir_len)) return 0;
    const char *rel = file->name + dir_len;
    if (dir_len) rel++;
    if (!rel[0]) return 0;
    uint64_t child_len = 0;
    while (rel[child_len] && rel[child_len] != '/') child_len++;
    copy_name_len(name, rel, child_len);
    if (rel[child_len] == '/') {
        *type = ORE_VFS_DIR;
        *size = 0;
    } else {
        *type = file->type;
        *size = file->size;
    }
    return 1;
}

static int disk_path(const char *path, const char **rel) {
    if (!path) return 0;
    if (str_eq_local(path, "/disk")) {
        *rel = "";
        return 1;
    }
    if (path[0] == '/' && path[1] == 'd' && path[2] == 'i' && path[3] == 's' &&
        path[4] == 'k' && path[5] == '/') {
        *rel = path + 6;
        return 1;
    }
    return 0;
}

static const OreFsEntry *disk_lookup(const char *path) {
    const char *rel = path;
    if (!disk_path(path, &rel)) rel = path;
    if (rel[0] == '/') rel++;
    for (uint32_t i = 0; i < disk_entry_count; ++i) {
        if (str_eq_local(disk_entries[i].name, rel)) return &disk_entries[i];
    }
    return 0;
}

static int disk_has_children(const char *rel) {
    uint64_t len = str_len_local(rel);
    if (!len) return disk_entry_count != 0;
    for (uint32_t i = 0; i < disk_entry_count; ++i) {
        if (str_prefix_eq(disk_entries[i].name, rel, len) && disk_entries[i].name[len] == '/') return 1;
    }
    return 0;
}

static int disk_direct_child(const char *dir, const OreFsEntry *entry, char name[128], uint32_t *type, uint64_t *size) {
    uint64_t dir_len = str_len_local(dir);
    if (dir_len) {
        if (!str_prefix_eq(entry->name, dir, dir_len) || entry->name[dir_len] != '/') return 0;
    }
    const char *rel = entry->name + dir_len;
    if (dir_len) rel++;
    if (!rel[0]) return 0;
    uint64_t child_len = 0;
    while (rel[child_len] && rel[child_len] != '/') child_len++;
    copy_name_len(name, rel, child_len);
    if (rel[child_len] == '/') {
        *type = ORE_VFS_DIR;
        *size = 0;
    } else {
        *type = entry->type;
        *size = entry->size;
    }
    return 1;
}

static int mount_disk(void) {
    uint8_t sector[512];
    if (block_root_read(0, sector, 1) < 0) return -1;
    OreFsHeader *hdr = (OreFsHeader *)sector;
    if (!(hdr->magic[0] == 'O' && hdr->magic[1] == 'R' && hdr->magic[2] == 'E' &&
          hdr->magic[3] == 'F' && hdr->magic[4] == 'S' && hdr->magic[5] == '1')) return -1;
    if (hdr->count > OREFS_MAX_ENTRIES) return -1;
    uint32_t entry_bytes = hdr->count * sizeof(OreFsEntry);
    uint32_t entry_sectors = (entry_bytes + 511) / 512;
    if (entry_sectors) {
        uint8_t *tmp = kmalloc(entry_sectors * 512);
        if (!tmp) return -1;
        if (block_root_read(1, tmp, entry_sectors) < 0) {
            kfree(tmp);
            return -1;
        }
        mem_copy_local(disk_entries, tmp, entry_bytes);
        kfree(tmp);
    }
    disk_entry_count = hdr->count;
    disk_mounted = 1;
    kprintf("vfs: /disk mounted orefs files %u\n", disk_entry_count);
    return 0;
}

void vfs_init(void) {
    mounted = 0;
    kprintf("vfs: initialized\n");
}

int vfs_mount_initramfs(void) {
    mounted = 1;
    kprintf("vfs: initramfs mounted files %u\n", initramfs_count());
    if (mount_disk() < 0) kprintf("vfs: /disk not mounted\n");
    return 0;
}

int vfs_stat(const char *path, VfsStat *stat) {
    if (!mounted || !path || !stat) return -1;
    if (path[0] == '/' && path[1] == 0) {
        copy_name(stat->name, "/");
        stat->type = ORE_VFS_DIR;
        stat->size = 0;
        return 0;
    }
    const char *rel = 0;
    if (disk_mounted && disk_path(path, &rel)) {
        if (!rel[0]) {
            copy_name(stat->name, "disk");
            stat->type = ORE_VFS_DIR;
            stat->size = 0;
            return 0;
        }
        const OreFsEntry *entry = disk_lookup(rel);
        if (entry) {
            copy_name(stat->name, entry->name);
            stat->type = entry->type;
            stat->size = entry->size;
            return 0;
        }
        if (disk_has_children(rel)) {
            copy_name(stat->name, rel);
            stat->type = ORE_VFS_DIR;
            stat->size = 0;
            return 0;
        }
        return -1;
    }
    const InitramfsFileView *file = initramfs_lookup_raw(path);
    if (!file) {
        if (!path_has_children(path)) return -1;
        const char *p = path[0] == '/' ? path + 1 : path;
        copy_name(stat->name, p);
        stat->type = ORE_VFS_DIR;
        stat->size = 0;
        return 0;
    }
    copy_name(stat->name, file->name);
    stat->type = file->type;
    stat->size = file->size;
    return 0;
}

int64_t vfs_read_file(const char *path, void *buffer, uint64_t buffer_size) {
    return vfs_read_file_at(path, 0, buffer, buffer_size);
}

int64_t vfs_read_file_at(const char *path, uint64_t offset, void *buffer, uint64_t buffer_size) {
    if (!mounted || !path || !buffer) return -1;
    const char *rel = 0;
    if (disk_mounted && disk_path(path, &rel)) {
        const OreFsEntry *entry = disk_lookup(rel);
        if (!entry || entry->type != ORE_VFS_FILE) return -1;
        if (offset >= entry->size) return 0;
        uint64_t remaining = entry->size - offset;
        uint64_t n = remaining < buffer_size ? remaining : buffer_size;
        uint64_t disk_offset = entry->offset + offset;
        uint64_t first_lba = disk_offset / 512;
        uint64_t lba_off = disk_offset % 512;
        uint64_t sectors = (lba_off + n + 511) / 512;
        uint8_t *tmp = kmalloc(sectors * 512);
        if (!tmp) return -1;
        if (block_root_read(first_lba, tmp, (uint32_t)sectors) < 0) {
            kfree(tmp);
            return -1;
        }
        mem_copy_local(buffer, tmp + lba_off, n);
        kfree(tmp);
        return (int64_t)n;
    }
    const InitramfsFileView *file = initramfs_lookup_raw(path);
    if (!file) return -1;
    if (offset >= file->size) return 0;
    uint64_t remaining = file->size - offset;
    uint64_t n = remaining < buffer_size ? remaining : buffer_size;
    mem_copy_local(buffer, file->data + offset, n);
    return (int64_t)n;
}

int vfs_readdir(uint32_t index, VfsStat *stat) {
    return vfs_readdir_path("/", index, stat);
}

int vfs_readdir_path(const char *path, uint32_t index, VfsStat *stat) {
    if (!mounted || !stat) return -1;
    const char *disk_rel = 0;
    if (disk_mounted && disk_path(path ? path : "/", &disk_rel)) {
        uint32_t emitted = 0;
        for (uint32_t i = 0; i < disk_entry_count; ++i) {
            char child[128];
            uint32_t type = 0;
            uint64_t size = 0;
            if (!disk_direct_child(disk_rel, &disk_entries[i], child, &type, &size)) continue;
            int duplicate = 0;
            for (uint32_t j = 0; j < i; ++j) {
                char prev_child[128];
                uint32_t prev_type = 0;
                uint64_t prev_size = 0;
                if (disk_direct_child(disk_rel, &disk_entries[j], prev_child, &prev_type, &prev_size) &&
                    str_eq_local(child, prev_child)) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) continue;
            if (emitted++ == index) {
                copy_name(stat->name, child);
                stat->type = type;
                stat->size = size;
                return 0;
            }
        }
        return -1;
    }
    char dir[128];
    const char *p = path ? path : "/";
    if (p[0] == '/') p++;
    copy_name(dir, p);

    uint32_t emitted = 0;
    for (uint32_t i = 0; i < initramfs_count(); ++i) {
        const InitramfsFileView *file = initramfs_file(i);
        char child[128];
        uint32_t type = 0;
        uint64_t size = 0;
        if (!file || !direct_child(dir, file, child, &type, &size)) continue;

        int duplicate = 0;
        for (uint32_t j = 0; j < i; ++j) {
            const InitramfsFileView *prev = initramfs_file(j);
            char prev_child[128];
            uint32_t prev_type = 0;
            uint64_t prev_size = 0;
            if (prev && direct_child(dir, prev, prev_child, &prev_type, &prev_size) && str_eq_local(child, prev_child)) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;
        if (emitted++ == index) {
            copy_name(stat->name, child);
            stat->type = type;
            stat->size = size;
            return 0;
        }
    }
    if (dir[0] == 0) {
        uint32_t root_extra = emitted;
        if (disk_mounted && index == root_extra) {
            copy_name(stat->name, "disk");
            stat->type = ORE_VFS_DIR;
            stat->size = 0;
            return 0;
        }
    }
    return -1;
}

void vfs_self_test(void) {
    VfsStat st;
    if (vfs_stat("/", &st) < 0) panic("vfs: root stat failed");
    if (initramfs_count() == 0) {
        kprintf("vfs: self-test ok (empty initramfs)\n");
        return;
    }
    const InitramfsFileView *file = initramfs_file(0);
    if (!file) panic("vfs: missing first file");
    char buf[32];
    int64_t n = vfs_read_file(file->name, buf, sizeof(buf));
    if (n < 0) panic("vfs: read failed");
    kprintf("vfs: self-test ok first %s bytes 0x%lx\n", file->name, (uint64_t)n);
}
