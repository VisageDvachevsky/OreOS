#include "kernel.h"

#define EI_NIDENT 16
#define PT_LOAD 1
#define ET_EXEC 2
#define EM_X86_64 62

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

static int elf_validate(const void *image, uint64_t size) {
    if (!image || size < sizeof(Elf64_Ehdr)) return -1;
    const Elf64_Ehdr *eh = image;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') return -1;
    if (eh->e_ident[4] != 2 || eh->e_ident[5] != 1) return -1;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64) return -1;
    if (eh->e_phoff + (uint64_t)eh->e_phentsize * eh->e_phnum > size) return -1;
    return 0;
}

int elf_probe_user_image(const void *image, uint64_t size, UserElfInfo *info) {
    if (!info || elf_validate(image, size) < 0) return -1;
    const Elf64_Ehdr *eh = image;
    const uint8_t *bytes = image;
    info->entry = eh->e_entry;
    info->image_size = size;
    info->segment_count = 0;
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(bytes + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || !ph->p_memsz) continue;
        if (ph->p_offset + ph->p_filesz > size || ph->p_filesz > ph->p_memsz) return -1;
        if (info->segment_count >= 8) return -1;
        UserElfSegmentInfo *seg = &info->segments[info->segment_count++];
        seg->vaddr = ph->p_vaddr;
        seg->file_offset = ph->p_offset;
        seg->mem_size = ph->p_memsz;
        seg->file_size = ph->p_filesz;
        seg->flags = ph->p_flags;
    }
    return info->segment_count ? 0 : -1;
}

void elf_self_test(void) {
    Elf64_Ehdr eh;
    uint8_t *p = (uint8_t *)&eh;
    for (uint64_t i = 0; i < sizeof(eh); ++i) p[i] = 0;
    eh.e_ident[0] = 0x7f;
    eh.e_ident[1] = 'E';
    eh.e_ident[2] = 'L';
    eh.e_ident[3] = 'F';
    eh.e_ident[4] = 2;
    eh.e_ident[5] = 1;
    eh.e_type = ET_EXEC;
    eh.e_machine = EM_X86_64;
    eh.e_phoff = sizeof(Elf64_Ehdr);
    eh.e_phentsize = 56;
    eh.e_phnum = 0;
    if (elf_validate(&eh, sizeof(eh)) < 0) panic("elf: valid self-test failed");
    eh.e_ident[1] = 'X';
    if (elf_validate(&eh, sizeof(eh)) == 0) panic("elf: invalid self-test failed");
    kprintf("elf: parser self-test ok\n");
}
