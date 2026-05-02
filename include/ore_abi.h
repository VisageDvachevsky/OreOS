#ifndef ORE_ABI_H
#define ORE_ABI_H

#include <stdint.h>

#define ORE_ENOENT 2
#define ORE_EINVAL 22
#define ORE_EBADF 9
#define ORE_EFAULT 14
#define ORE_ENOSYS 38
#define ORE_ENOMEM 12

#define ORE_SYS_WRITE 1
#define ORE_SYS_READ 2
#define ORE_SYS_EXIT 3
#define ORE_SYS_YIELD 4
#define ORE_SYS_OPEN 5
#define ORE_SYS_READDIR 6
#define ORE_SYS_STAT 7
#define ORE_SYS_CLOSE 8
#define ORE_SYS_FILE_READ 9
#define ORE_SYS_INFO 10
#define ORE_SYS_SPAWN 11
#define ORE_SYS_WAIT 12
#define ORE_SYS_GETPID 13
#define ORE_SYS_SLEEP 14
#define ORE_SYS_PROC_INFO 15
#define ORE_SYS_ARGS 16
#define ORE_SYS_EXEC_IMAGE 17
#define ORE_SYS_MOD_INFO 18
#define ORE_SYS_KCMD 19
#define ORE_SYS_GFX_INFO 20
#define ORE_SYS_GFX_PRESENT 21
#define ORE_SYS_INPUT_STATE 22
#define ORE_SYS_USER_ALLOC_PAGES 23
#define ORE_SYS_USER_FREE_PAGES 24
#define ORE_SYS_TERRAIN_RENDER 25
#define ORE_SYS_NET_INFO 26
#define ORE_SYS_NET_RECV 27
#define ORE_SYS_NET_SEND 28

#define ORE_VFS_FILE 1
#define ORE_VFS_DIR 2

#define ORE_PROC_UNUSED 0
#define ORE_PROC_READY 1
#define ORE_PROC_RUNNING 2
#define ORE_PROC_EXITED 3
#define ORE_PROC_BLOCKED 4

typedef struct {
    char name[128];
    uint32_t type;
    uint32_t reserved;
    uint64_t size;
} OreVfsStat;

typedef struct {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t reserved_pages;
    uint64_t uptime_ticks;
    uint32_t cpu_count;
    uint32_t pid;
} OreSysInfo;

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    int32_t exit_code;
    uint32_t segment_count;
    uint64_t syscall_count;
    uint64_t entry;
    uint64_t stack;
    char name[32];
} OreProcessInfo;

typedef struct {
    char name[32];
    char path[128];
    uint32_t state;
    uint32_t abi_version;
    int32_t init_rc;
    uint32_t reserved;
    uint64_t text_base;
    uint64_t text_size;
    uint64_t data_size;
    uint32_t has_command;
    uint32_t reserved2;
} OreModuleInfo;

#define ORE_GFX_FORMAT_INDEXED16 1
#define ORE_GFX_LOGICAL_WIDTH 640
#define ORE_GFX_LOGICAL_HEIGHT 480

#define ORE_INPUT_LEFT  (1U << 0)
#define ORE_INPUT_RIGHT (1U << 1)
#define ORE_INPUT_UP    (1U << 2)
#define ORE_INPUT_DOWN  (1U << 3)
#define ORE_INPUT_FIRE  (1U << 4)
#define ORE_INPUT_QUIT  (1U << 5)

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t logical_width;
    uint32_t logical_height;
    uint32_t format;
    uint32_t pitch;
    uint32_t palette_count;
    uint32_t reserved;
} OreGfxInfo;

typedef struct {
    uint32_t keys;
    uint32_t ascii;
    int32_t mouse_x;
    int32_t mouse_y;
    uint32_t buttons;
    uint32_t frame;
} OreInputState;

typedef struct {
    uint64_t seed;
    uint64_t frame;
    int32_t camera_x;
    int32_t camera_y;
    int32_t camera_z;
    int32_t yaw;
    int32_t pitch;
    int32_t speed;
    uint32_t flags;
    uint32_t reserved;
} OreTerrainJob;

typedef struct {
    uint32_t workers_used;
    uint32_t objects_rendered;
    uint32_t collision_flags;
    uint32_t reserved;
    uint64_t checksum;
    uint64_t render_ticks;
} OreTerrainResult;

#define ORE_NET_MAX_FRAME 1518

typedef struct {
    uint32_t ready;
    uint32_t mtu;
    uint8_t mac[6];
    uint8_t reserved[2];
    uint32_t ip_be;
    uint32_t gateway_be;
    uint32_t dns_be;
} OreNetInfo;

#define ORE_IMAGE_MAGIC 0x31474d4945524fULL /* OREIMG1, little endian */
#define ORE_IMAGE_VERSION 1
#define ORE_IMAGE_RELOC_ABS64 1
#define ORE_IMAGE_RELOC_PC32 2

#define ORE_KMOD_MAGIC 0x31444f4d4b45524fULL /* OREKMOD1, little endian */
#define ORE_KMOD_VERSION 1
#define ORE_KERNEL_ABI_VERSION 1

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t header_size;
    uint64_t entry;
    uint64_t text_size;
    uint64_t data_size;
    uint64_t bss_size;
    uint64_t reloc_count;
} OreImageHeader;

typedef struct {
    uint64_t offset;
    uint64_t symbol_offset;
    uint32_t type;
    uint32_t segment;
} OreImageReloc;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t kernel_abi_version;
    uint32_t flags;
    uint64_t entry;
    uint64_t text_size;
    uint64_t data_size;
    uint64_t bss_size;
    uint64_t reloc_count;
    uint64_t import_count;
    uint64_t export_count;
    uint64_t command_entry;
    char name[32];
} OreKModHeader;

typedef struct {
    void (*log)(const char *s);
    void *(*alloc)(uint64_t size);
    void (*free)(void *ptr);
    int (*vfs_stat)(const char *path, OreVfsStat *stat);
    int64_t (*vfs_read_file_at)(const char *path, uint64_t offset, void *buffer, uint64_t buffer_size);
    int (*vfs_readdir_path)(const char *path, uint32_t index, OreVfsStat *stat);
} OreKernelApi;

#endif
