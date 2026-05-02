#ifndef ORE_BOOTINFO_H
#define ORE_BOOTINFO_H

#include <stdint.h>

#define ORE_BOOTINFO_MAGIC 0x4f524542494e464fULL
#define ORE_MEMMAP_MAX 256

typedef struct {
    uint64_t type;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} OreMemoryDescriptor;

typedef struct {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t pixel_format;
} OreFramebuffer;

typedef struct {
    uint64_t magic;
    uint64_t rsdp;
    uint64_t kernel_base;
    uint64_t kernel_size;
    uint64_t initramfs_base;
    uint64_t initramfs_size;
    OreFramebuffer framebuffer;
    uint64_t memory_map_entries;
    uint64_t memory_map_descriptor_size;
    OreMemoryDescriptor memory_map[ORE_MEMMAP_MAX];
    char cmdline[128];
} BootInfo;

#endif
