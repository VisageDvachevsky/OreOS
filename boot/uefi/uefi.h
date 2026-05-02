#ifndef ORE_UEFI_H
#define ORE_UEFI_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef uint64_t UINTN;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;

#define EFI_SUCCESS 0
#define EFI_ERROR_BIT 0x8000000000000000ULL
#define EFI_ERROR(x) (((x) & EFI_ERROR_BIT) != 0)
#define EFI_LOAD_ERROR (EFI_ERROR_BIT | 1)
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5)
#define EFI_NOT_FOUND (EFI_ERROR_BIT | 14)

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001
#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_ALLOCATE_ADDRESS 0
#define EFI_LOADER_DATA 2

typedef struct { uint32_t data1; uint16_t data2; uint16_t data3; uint8_t data4[8]; } EFI_GUID;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint32_t type;
    EFI_PHYSICAL_ADDRESS physical_start;
    EFI_VIRTUAL_ADDRESS virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *reset;
    EFI_STATUS (__attribute__((ms_abi)) *output_string)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
};

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    uint64_t revision;
    EFI_STATUS (__attribute__((ms_abi)) *open)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, uint64_t, uint64_t);
    EFI_STATUS (__attribute__((ms_abi)) *close)(EFI_FILE_PROTOCOL *);
    void *delete_file;
    EFI_STATUS (__attribute__((ms_abi)) *read)(EFI_FILE_PROTOCOL *, UINTN *, void *);
};

typedef struct {
    uint64_t revision;
    EFI_STATUS (__attribute__((ms_abi)) *open_volume)(void *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    void *info;
    UINTN size_of_info;
    EFI_PHYSICAL_ADDRESS frame_buffer_base;
    UINTN frame_buffer_size;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    uint32_t pixel_information[4];
    uint32_t pixels_per_scanline;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    void *query_mode;
    void *set_mode;
    void *blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    EFI_TABLE_HEADER hdr;
    CHAR16 *firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    void *con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *con_out;
    EFI_HANDLE standard_error_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *std_err;
    void *runtime_services;
    struct EFI_BOOT_SERVICES *boot_services;
    UINTN number_of_table_entries;
    struct EFI_CONFIGURATION_TABLE *configuration_table;
} EFI_SYSTEM_TABLE;

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER hdr;
    void *raise_tpl; void *restore_tpl;
    EFI_STATUS (__attribute__((ms_abi)) *allocate_pages)(int, int, UINTN, EFI_PHYSICAL_ADDRESS *);
    EFI_STATUS (__attribute__((ms_abi)) *free_pages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS (__attribute__((ms_abi)) *get_memory_map)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, uint32_t *);
    EFI_STATUS (__attribute__((ms_abi)) *allocate_pool)(int, UINTN, void **);
    EFI_STATUS (__attribute__((ms_abi)) *free_pool)(void *);
    void *create_event; void *set_timer; void *wait_for_event; void *signal_event; void *close_event; void *check_event;
    void *install_protocol_interface; void *reinstall_protocol_interface; void *uninstall_protocol_interface;
    EFI_STATUS (__attribute__((ms_abi)) *handle_protocol)(EFI_HANDLE, EFI_GUID *, void **);
    void *reserved;
    void *register_protocol_notify;
    EFI_STATUS (__attribute__((ms_abi)) *locate_handle)(int, EFI_GUID *, void *, UINTN *, EFI_HANDLE *);
    EFI_STATUS (__attribute__((ms_abi)) *locate_device_path)(EFI_GUID *, void **, EFI_HANDLE *);
    EFI_STATUS (__attribute__((ms_abi)) *install_configuration_table)(EFI_GUID *, void *);
    void *load_image; void *start_image; void *exit; void *unload_image;
    EFI_STATUS (__attribute__((ms_abi)) *exit_boot_services)(EFI_HANDLE, UINTN);
    void *get_next_monotonic_count; void *stall; void *set_watchdog_timer;
    EFI_STATUS (__attribute__((ms_abi)) *connect_controller)(EFI_HANDLE, EFI_HANDLE *, void *, int);
    void *disconnect_controller;
    EFI_STATUS (__attribute__((ms_abi)) *open_protocol)(EFI_HANDLE, EFI_GUID *, void **, EFI_HANDLE, EFI_HANDLE, uint32_t);
    void *close_protocol; void *open_protocol_information;
    void *protocols_per_handle;
    EFI_STATUS (__attribute__((ms_abi)) *locate_handle_buffer)(int, EFI_GUID *, void *, UINTN *, EFI_HANDLE **);
    EFI_STATUS (__attribute__((ms_abi)) *locate_protocol)(EFI_GUID *, void *, void **);
} EFI_BOOT_SERVICES;

typedef struct EFI_CONFIGURATION_TABLE {
    EFI_GUID vendor_guid;
    void *vendor_table;
} EFI_CONFIGURATION_TABLE;

#endif
