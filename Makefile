SHELL := /bin/sh

BUILD := build
ESP := $(BUILD)/esp.img
DISK := $(BUILD)/disk.img
SMP ?= 4
KERNEL_TEST_EXCEPTION ?= 0

CLANG ?= clang
LLD_LINK ?= lld-link
LD_LLD ?= ld.lld
OBJCOPY ?= objcopy

UEFI_CFLAGS := -target x86_64-unknown-windows -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin -Wall -Wextra -Iinclude -Iboot/uefi
KERNEL_CFLAGS := -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Wall -Wextra -Iinclude -Ikernel -DORE_TEST_EXCEPTION=$(KERNEL_TEST_EXCEPTION)

KERNEL_OBJS := \
	$(BUILD)/kernel/entry.o \
	$(BUILD)/kernel/isr.o \
	$(BUILD)/kernel/thread_entry.o \
	$(BUILD)/kernel/user_entry.o \
	$(BUILD)/kernel/ap_trampoline.o \
	$(BUILD)/kernel/main.o \
	$(BUILD)/kernel/serial.o \
	$(BUILD)/kernel/cpu.o \
	$(BUILD)/kernel/panic.o \
	$(BUILD)/kernel/gdt.o \
	$(BUILD)/kernel/idt.o \
	$(BUILD)/kernel/lapic.o \
	$(BUILD)/kernel/spinlock.o \
	$(BUILD)/kernel/vmm.o \
	$(BUILD)/kernel/process.o \
	$(BUILD)/kernel/scheduler.o \
	$(BUILD)/kernel/acpi.o \
	$(BUILD)/kernel/smp.o \
	$(BUILD)/kernel/pmm.o \
	$(BUILD)/kernel/heap.o \
	$(BUILD)/kernel/memory_test.o \
	$(BUILD)/kernel/initramfs.o \
	$(BUILD)/kernel/vfs.o \
	$(BUILD)/kernel/okmod.o \
	$(BUILD)/kernel/console.o \
	$(BUILD)/kernel/terrain.o \
	$(BUILD)/kernel/net.o \
	$(BUILD)/kernel/elf.o \
	$(BUILD)/kernel/syscall.o \
	$(BUILD)/kernel/block.o \
	$(BUILD)/kernel/user.o

.PHONY: all build image disk disk-reset run run-gui check clean check-tools

all: image

build: check-tools $(BUILD)/BOOTX64.EFI $(BUILD)/kernel.elf
	@echo "Built loader and kernel"

check-tools:
	@command -v $(CLANG) >/dev/null || { echo "missing clang"; exit 1; }
	@command -v $(LLD_LINK) >/dev/null || { echo "missing lld-link"; exit 1; }
	@command -v $(LD_LLD) >/dev/null || { echo "missing ld.lld"; exit 1; }
	@command -v $(OBJCOPY) >/dev/null || { echo "missing objcopy"; exit 1; }

prepare:
	mkdir -p $(BUILD)/boot/uefi $(BUILD)/kernel

$(BUILD)/boot/uefi/loader.o: boot/uefi/loader.c include/bootinfo.h boot/uefi/uefi.h | prepare
	$(CLANG) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD)/BOOTX64.EFI: $(BUILD)/boot/uefi/loader.o
	$(LLD_LINK) /nologo /subsystem:efi_application /entry:efi_main /nodefaultlib $< /out:$@

$(BUILD)/kernel/%.o: kernel/%.c include/bootinfo.h include/ore_abi.h kernel/kernel.h | prepare
	$(CLANG) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD)/kernel/entry.o: arch/x86_64/entry.S | prepare
	$(CLANG) -target x86_64-unknown-none -ffreestanding -mno-red-zone -c $< -o $@

$(BUILD)/kernel/isr.o: arch/x86_64/isr.S | prepare
	$(CLANG) -target x86_64-unknown-none -ffreestanding -mno-red-zone -c $< -o $@

$(BUILD)/kernel/thread_entry.o: arch/x86_64/thread_entry.S | prepare
	$(CLANG) -target x86_64-unknown-none -ffreestanding -mno-red-zone -c $< -o $@

$(BUILD)/kernel/user_entry.o: arch/x86_64/user_entry.S | prepare
	$(CLANG) -target x86_64-unknown-none -ffreestanding -mno-red-zone -c $< -o $@

$(BUILD)/kernel/ap_trampoline.o: arch/x86_64/ap_trampoline.S | prepare
	$(CLANG) -target x86_64-unknown-none -ffreestanding -mno-red-zone -c $< -o $@

$(BUILD)/kernel.elf: linker.ld $(KERNEL_OBJS)
	$(LD_LLD) -nostdlib -T linker.ld -o $@ $(KERNEL_OBJS)

$(BUILD)/user/init.o: user/init.c user/user.h include/ore_abi.h | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -Iuser -Iinclude -c $< -o $@

$(BUILD)/user/hello.o: user/hello.c user/user.h include/ore_abi.h | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -Iuser -Iinclude -c $< -o $@

$(BUILD)/user/count.o: user/count.c user/user.h include/ore_abi.h | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -Iuser -Iinclude -c $< -o $@

$(BUILD)/user/ls.o: user/ls.c user/user.h include/ore_abi.h | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -Iuser -Iinclude -c $< -o $@

$(BUILD)/user/cat.o: user/cat.c user/user.h include/ore_abi.h | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -Iuser -Iinclude -c $< -o $@

$(BUILD)/user/echo.o: user/echo.c user/user.h include/ore_abi.h | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -Iuser -Iinclude -c $< -o $@

$(BUILD)/user/stat.o: user/stat.c user/user.h include/ore_abi.h | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -Iuser -Iinclude -c $< -o $@

$(BUILD)/user/orec.o: user/orec.c user/user.h include/ore_abi.h | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -Iuser -Iinclude -c $< -o $@

$(BUILD)/orec-host: user/orec.c include/ore_abi.h | prepare
	$(CLANG) -DOREC_HOST -O2 -Wall -Wextra -Iinclude -o $@ user/orec.c

$(BUILD)/user/crt0.o: user/crt0.S | prepare
	mkdir -p $(BUILD)/user
	$(CLANG) -target x86_64-unknown-none -ffreestanding -mno-red-zone -mcmodel=large -c $< -o $@

$(BUILD)/init: user/linker.ld $(BUILD)/user/crt0.o $(BUILD)/user/init.o
	$(LD_LLD) -nostdlib -T user/linker.ld -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/init.o

$(BUILD)/hello: user/linker.ld $(BUILD)/user/crt0.o $(BUILD)/user/hello.o
	$(LD_LLD) -nostdlib -T user/linker.ld -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/hello.o

$(BUILD)/count: user/linker.ld $(BUILD)/user/crt0.o $(BUILD)/user/count.o
	$(LD_LLD) -nostdlib -T user/linker.ld -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/count.o

$(BUILD)/ls: user/linker.ld $(BUILD)/user/crt0.o $(BUILD)/user/ls.o
	$(LD_LLD) -nostdlib -T user/linker.ld -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/ls.o

$(BUILD)/cat: user/linker.ld $(BUILD)/user/crt0.o $(BUILD)/user/cat.o
	$(LD_LLD) -nostdlib -T user/linker.ld -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/cat.o

$(BUILD)/echo: user/linker.ld $(BUILD)/user/crt0.o $(BUILD)/user/echo.o
	$(LD_LLD) -nostdlib -T user/linker.ld -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/echo.o

$(BUILD)/stat: user/linker.ld $(BUILD)/user/crt0.o $(BUILD)/user/stat.o
	$(LD_LLD) -nostdlib -T user/linker.ld -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/stat.o

$(BUILD)/orec: user/linker.ld $(BUILD)/user/crt0.o $(BUILD)/user/orec.o
	$(LD_LLD) -nostdlib -T user/linker.ld -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/orec.o

$(BUILD)/initramfs.tar: initramfs/init.txt initramfs/etc/banner initramfs/docs/readme $(BUILD)/init $(BUILD)/hello $(BUILD)/count $(BUILD)/ls $(BUILD)/cat $(BUILD)/echo $(BUILD)/stat $(BUILD)/orec | prepare
	cp $(BUILD)/init initramfs/init
	mkdir -p initramfs/bin
	cp $(BUILD)/init initramfs/bin/init
	cp $(BUILD)/hello initramfs/bin/hello
	cp $(BUILD)/count initramfs/bin/count
	cp $(BUILD)/ls initramfs/bin/ls
	cp $(BUILD)/cat initramfs/bin/cat
	cp $(BUILD)/echo initramfs/bin/echo
	cp $(BUILD)/stat initramfs/bin/stat
	cp $(BUILD)/orec initramfs/bin/orec
	tar -cf $@ -C initramfs init.txt init bin/init bin/hello bin/ls bin/cat bin/echo bin/stat bin/count bin/orec etc/banner docs/readme

disk: $(BUILD)/orec-host
	python3 tools/mkorefs.py $(DISK) $(BUILD)/orec-host

disk-reset: $(BUILD)/orec-host
	rm -f $(DISK)
	python3 tools/mkorefs.py $(DISK) $(BUILD)/orec-host

check: disk
	python3 tools/test_http_app.py

image: check-tools $(BUILD)/BOOTX64.EFI $(BUILD)/kernel.elf $(BUILD)/initramfs.tar disk
	@command -v mformat >/dev/null || { echo "missing mtools: sudo apt install mtools"; exit 1; }
	@rm -f $(ESP)
	dd if=/dev/zero of=$(ESP) bs=1M count=64 status=none
	mformat -i $(ESP) -F ::
	mmd -i $(ESP) ::/EFI ::/EFI/BOOT
	mcopy -i $(ESP) $(BUILD)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(ESP) $(BUILD)/kernel.elf ::/kernel.elf
	mcopy -i $(ESP) $(BUILD)/initramfs.tar ::/initramfs.tar
	@echo "Built $(ESP)"

run: image
	@command -v qemu-system-x86_64 >/dev/null || { echo "missing qemu-system-x86_64: sudo apt install qemu-system-x86 ovmf"; exit 1; }
	@if [ -f /usr/share/qemu/OVMF.fd ]; then OVMF=/usr/share/qemu/OVMF.fd; \
	elif [ -f /usr/share/ovmf/OVMF.fd ]; then OVMF=/usr/share/ovmf/OVMF.fd; \
	elif [ -f /usr/share/OVMF/OVMF_CODE.fd ]; then OVMF=/usr/share/OVMF/OVMF_CODE.fd; \
	else echo "missing OVMF firmware: sudo apt install ovmf"; exit 1; fi; \
	qemu-system-x86_64 -machine q35 -cpu qemu64 -smp $(SMP) -m 256M \
		-bios $$OVMF -drive if=ide,format=raw,file=$(ESP) \
		-fw_cfg name=opt/ore/disk,file=$(DISK) \
		-netdev user,id=net0 -device ne2k_isa,netdev=net0 \
		-serial stdio -display none -no-reboot -no-shutdown

run-gui: image
	@command -v qemu-system-x86_64 >/dev/null || { echo "missing qemu-system-x86_64: sudo apt install qemu-system-x86 ovmf"; exit 1; }
	@if [ -f /usr/share/qemu/OVMF.fd ]; then OVMF=/usr/share/qemu/OVMF.fd; \
	elif [ -f /usr/share/ovmf/OVMF.fd ]; then OVMF=/usr/share/ovmf/OVMF.fd; \
	elif [ -f /usr/share/OVMF/OVMF_CODE.fd ]; then OVMF=/usr/share/OVMF/OVMF_CODE.fd; \
	else echo "missing OVMF firmware: sudo apt install ovmf"; exit 1; fi; \
	qemu-system-x86_64 -machine q35 -cpu qemu64 -smp $(SMP) -m 256M \
		-bios $$OVMF -drive if=ide,format=raw,file=$(ESP) \
		-fw_cfg name=opt/ore/disk,file=$(DISK) \
		-netdev user,id=net0 -device ne2k_isa,netdev=net0 \
		-serial stdio -display gtk -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD)
