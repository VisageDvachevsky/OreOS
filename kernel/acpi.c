#include "kernel.h"

#define MAX_CPUS 64

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
} __attribute__((packed)) Rsdp;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) SdtHeader;

typedef struct {
    SdtHeader header;
    uint32_t lapic_addr;
    uint32_t flags;
} __attribute__((packed)) Madt;

static uint8_t cpu_apic_ids[MAX_CPUS];
static uint32_t cpu_count_value;
static uint64_t lapic_base_value = 0xfee00000;

static int sig_eq(const char *a, const char *b) {
    for (int i = 0; i < 4; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static uint8_t checksum(const void *ptr, uint32_t len) {
    const uint8_t *p = ptr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) sum = (uint8_t)(sum + p[i]);
    return sum;
}

static void parse_madt(Madt *madt) {
    if (checksum(madt, madt->header.length) != 0) {
        kprintf("ACPI: MADT checksum failed\n");
        return;
    }
    lapic_base_value = madt->lapic_addr;
    uint8_t *p = (uint8_t *)madt + sizeof(Madt);
    uint8_t *end = (uint8_t *)madt + madt->header.length;
    while (p + 2 <= end) {
        uint8_t type = p[0];
        uint8_t len = p[1];
        if (len < 2 || p + len > end) break;
        if (type == 0 && len >= 8) {
            uint8_t apic_id = p[3];
            uint32_t flags = *(uint32_t *)(p + 4);
            if ((flags & 1) && cpu_count_value < MAX_CPUS) {
                cpu_apic_ids[cpu_count_value++] = apic_id;
            }
        } else if (type == 5 && len >= 12) {
            lapic_base_value = *(uint64_t *)(p + 4);
        }
        p += len;
    }
}

void acpi_init(uint64_t rsdp_addr) {
    cpu_count_value = 0;
    if (!rsdp_addr) {
        kprintf("ACPI: no RSDP from loader\n");
        return;
    }
    Rsdp *rsdp = (Rsdp *)(uintptr_t)rsdp_addr;
    kprintf("ACPI: RSDP %p rev %u\n", rsdp, rsdp->revision);
    SdtHeader *root = rsdp->revision >= 2 && rsdp->xsdt_address
        ? (SdtHeader *)(uintptr_t)rsdp->xsdt_address
        : (SdtHeader *)(uintptr_t)rsdp->rsdt_address;
    if (!root || checksum(root, root->length) != 0) {
        kprintf("ACPI: root table checksum failed\n");
        return;
    }
    uint32_t entry_size = sig_eq(root->signature, "XSDT") ? 8 : 4;
    uint32_t entries = (root->length - sizeof(SdtHeader)) / entry_size;
    uint8_t *entry_base = (uint8_t *)root + sizeof(SdtHeader);
    for (uint32_t i = 0; i < entries; ++i) {
        uint64_t addr = entry_size == 8 ? *(uint64_t *)(entry_base + i * 8) : *(uint32_t *)(entry_base + i * 4);
        SdtHeader *h = (SdtHeader *)(uintptr_t)addr;
        if (sig_eq(h->signature, "APIC")) {
            parse_madt((Madt *)h);
            break;
        }
    }
    kprintf("ACPI: CPUs %u LAPIC 0x%lx\n", cpu_count_value, lapic_base_value);
}

uint32_t acpi_cpu_count(void) {
    return cpu_count_value;
}

uint8_t acpi_cpu_apic_id(uint32_t index) {
    return index < cpu_count_value ? cpu_apic_ids[index] : 0xff;
}

uint64_t acpi_lapic_base(void) {
    return lapic_base_value;
}
