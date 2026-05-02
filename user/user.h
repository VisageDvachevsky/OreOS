#ifndef ORE_USER_H
#define ORE_USER_H

#include <stdint.h>
#include <stddef.h>
#include "ore_abi.h"

typedef OreVfsStat VfsStat;
typedef OreSysInfo SysInfo;
typedef OreProcessInfo ProcessInfo;
typedef OreModuleInfo ModuleInfo;
typedef OreGfxInfo GfxInfo;
typedef OreInputState InputState;
typedef OreTerrainJob TerrainJob;
typedef OreTerrainResult TerrainResult;

uint64_t syscall6(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

static inline int64_t sys_write(uint64_t fd, const void *buf, uint64_t len) { return (int64_t)syscall6(ORE_SYS_WRITE, fd, (uint64_t)(uintptr_t)buf, len, 0, 0); }
static inline int64_t sys_read(uint64_t fd, void *buf, uint64_t len) { return (int64_t)syscall6(ORE_SYS_READ, fd, (uint64_t)(uintptr_t)buf, len, 0, 0); }
static inline void sys_exit(uint64_t code) { syscall6(ORE_SYS_EXIT, code, 0, 0, 0, 0); for (;;) {} }
static inline int64_t sys_yield(void) { return (int64_t)syscall6(ORE_SYS_YIELD, 0, 0, 0, 0, 0); }
static inline int64_t sys_open(const char *path) { return (int64_t)syscall6(ORE_SYS_OPEN, (uint64_t)(uintptr_t)path, 0, 0, 0, 0); }
static inline int64_t sys_readdir(uint32_t index, VfsStat *st) { return (int64_t)syscall6(ORE_SYS_READDIR, index, (uint64_t)(uintptr_t)st, 0, 0, 0); }
static inline int64_t sys_readdir_path(const char *path, uint32_t index, VfsStat *st) { return (int64_t)syscall6(ORE_SYS_READDIR, index, (uint64_t)(uintptr_t)st, (uint64_t)(uintptr_t)path, 0, 0); }
static inline int64_t sys_stat(const char *path, VfsStat *st) { return (int64_t)syscall6(ORE_SYS_STAT, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)st, 0, 0, 0); }
static inline int64_t sys_close(uint64_t fd) { return (int64_t)syscall6(ORE_SYS_CLOSE, fd, 0, 0, 0, 0); }
static inline int64_t sys_file_read(uint64_t fd, void *buf, uint64_t len) { return (int64_t)syscall6(ORE_SYS_FILE_READ, fd, (uint64_t)(uintptr_t)buf, len, 0, 0); }
static inline int64_t sys_info(SysInfo *info) { return (int64_t)syscall6(ORE_SYS_INFO, (uint64_t)(uintptr_t)info, 0, 0, 0, 0); }
static inline int64_t sys_spawn(const char *path) { return (int64_t)syscall6(ORE_SYS_SPAWN, (uint64_t)(uintptr_t)path, 0, 0, 0, 0); }
static inline int64_t sys_spawn_args(const char *path, const char *args) { return (int64_t)syscall6(ORE_SYS_SPAWN, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)args, 0, 0, 0); }
static inline int64_t sys_wait(uint64_t pid) { return (int64_t)syscall6(ORE_SYS_WAIT, pid, 0, 0, 0, 0); }
static inline int64_t sys_getpid(void) { return (int64_t)syscall6(ORE_SYS_GETPID, 0, 0, 0, 0, 0); }
static inline int64_t sys_sleep(uint64_t ticks) { return (int64_t)syscall6(ORE_SYS_SLEEP, ticks, 0, 0, 0, 0); }
static inline int64_t sys_proc_info(uint32_t index, ProcessInfo *info) { return (int64_t)syscall6(ORE_SYS_PROC_INFO, index, (uint64_t)(uintptr_t)info, 0, 0, 0); }
static inline int64_t sys_args(char *buf, uint64_t len) { return (int64_t)syscall6(ORE_SYS_ARGS, (uint64_t)(uintptr_t)buf, len, 0, 0, 0); }
static inline int64_t sys_exec_image(const void *image, uint64_t len, const char *args) { return (int64_t)syscall6(ORE_SYS_EXEC_IMAGE, (uint64_t)(uintptr_t)image, len, (uint64_t)(uintptr_t)args, 0, 0); }
static inline int64_t sys_mod_info(uint32_t index, ModuleInfo *info) { return (int64_t)syscall6(ORE_SYS_MOD_INFO, index, (uint64_t)(uintptr_t)info, 0, 0, 0); }
static inline int64_t sys_kcmd(const char *name, char *out, uint64_t out_len) { return (int64_t)syscall6(ORE_SYS_KCMD, (uint64_t)(uintptr_t)name, (uint64_t)(uintptr_t)out, out_len, 0, 0); }
static inline int64_t sys_gfx_info(GfxInfo *info) { return (int64_t)syscall6(ORE_SYS_GFX_INFO, (uint64_t)(uintptr_t)info, 0, 0, 0, 0); }
static inline int64_t sys_gfx_present(const void *buf, uint64_t len, uint32_t w, uint32_t h) { return (int64_t)syscall6(ORE_SYS_GFX_PRESENT, (uint64_t)(uintptr_t)buf, len, w, h, 0); }
static inline int64_t sys_input_state(InputState *state) { return (int64_t)syscall6(ORE_SYS_INPUT_STATE, (uint64_t)(uintptr_t)state, 0, 0, 0, 0); }
static inline void *sys_user_alloc_pages(uint64_t pages) { return (void *)(uintptr_t)syscall6(ORE_SYS_USER_ALLOC_PAGES, pages, 0, 0, 0, 0); }
static inline int64_t sys_user_free_pages(void *ptr, uint64_t pages) { return (int64_t)syscall6(ORE_SYS_USER_FREE_PAGES, (uint64_t)(uintptr_t)ptr, pages, 0, 0, 0); }
static inline int64_t sys_terrain_render(const TerrainJob *job, void *buf, uint64_t len, TerrainResult *result) { return (int64_t)syscall6(ORE_SYS_TERRAIN_RENDER, (uint64_t)(uintptr_t)job, (uint64_t)(uintptr_t)buf, len, (uint64_t)(uintptr_t)result, 0); }

uint64_t strlen(const char *s);
void puts(const char *s);
void printf(const char *fmt, ...);
int main(void);

#endif
