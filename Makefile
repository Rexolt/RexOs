# =============================================================================
#  Rex OS - Top-level Makefile
#  Phase 1: "Rex is awakening..."
# =============================================================================

KERNEL      := rexos.elf
ISO         := rexos.iso
DISK        := disk.img

# -----------------------------------------------------------------------------
#  Toolchain
# -----------------------------------------------------------------------------
CC          := gcc
LD          := ld
NASM        := nasm

# -----------------------------------------------------------------------------
#  Mappák
# -----------------------------------------------------------------------------
BUILD       := build
KDIR        := kernel
LIMINE_DIR  := boot/limine
ISO_ROOT    := $(BUILD)/iso_root
INITRD_TAR  := $(BUILD)/initrd.tar

# -----------------------------------------------------------------------------
#  Forrásfájlok automatikus felderítése
# -----------------------------------------------------------------------------
C_SRCS      := $(shell find $(KDIR) -name '*.c' 2>/dev/null)
ASM_SRCS    := $(shell find $(KDIR) -name '*.asm' 2>/dev/null)

C_OBJS      := $(patsubst $(KDIR)/%.c,$(BUILD)/%.c.o,$(C_SRCS))
ASM_OBJS    := $(patsubst $(KDIR)/%.asm,$(BUILD)/%.asm.o,$(ASM_SRCS))
OBJS        := $(C_OBJS) $(ASM_OBJS)

# -----------------------------------------------------------------------------
#  Fordítási flagek - freestanding x86_64 kernel
# -----------------------------------------------------------------------------
CFLAGS      := \
    -std=c17 \
    -Wall -Wextra -Wpedantic \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-PIC -fno-PIE \
    -m64 \
    -march=x86-64 \
    -mno-red-zone \
    -mno-mmx -mno-sse -mno-sse2 \
    -mcmodel=kernel \
    -I$(KDIR) \
    -I$(KDIR)/include \
    -I$(KDIR)/include/rexos \
    -I$(LIMINE_DIR) \
    -g -O2

LDFLAGS     := \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -no-pie \
    --no-dynamic-linker \
    -z max-page-size=0x1000 \
    -T $(KDIR)/linker.ld

NASMFLAGS   := -f elf64 -g

# -----------------------------------------------------------------------------
#  Targets
# -----------------------------------------------------------------------------
.PHONY: all clean run run-uefi iso help

all: $(ISO)

help:
	@echo "Rex OS build targets:"
	@echo "  make            - build the bootable ISO"
	@echo "  make run        - boot Rex OS in QEMU (BIOS mode)"
	@echo "  make run-uefi   - boot Rex OS in QEMU (UEFI mode, needs OVMF)"
	@echo "  make clean      - remove build artifacts"

# -----------------------------------------------------------------------------
#  Kernel ELF linkelés
# -----------------------------------------------------------------------------
$(BUILD)/$(KERNEL): $(OBJS) $(KDIR)/linker.ld
	@mkdir -p $(dir $@)
	@echo "  LD   $@"
	@$(LD) $(LDFLAGS) $(OBJS) -o $@

# -----------------------------------------------------------------------------
#  Object fordítás
# -----------------------------------------------------------------------------
$(BUILD)/%.c.o: $(KDIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: $(KDIR)/%.asm
	@mkdir -p $(dir $@)
	@echo "  ASM  $<"
	@$(NASM) $(NASMFLAGS) $< -o $@

# -----------------------------------------------------------------------------
#  Limine bootloader build (egyszer fut le, amíg törlöd)
# -----------------------------------------------------------------------------
$(LIMINE_DIR)/limine:
	@echo "  MAKE limine"
	@$(MAKE) -s -C $(LIMINE_DIR)

# -----------------------------------------------------------------------------
#  User Mode Programok építése
# -----------------------------------------------------------------------------
USER_CFLAGS = -Wall -Wextra -O2 -ffreestanding -fno-stack-protector -fno-pic -fpie -mno-red-zone -nostdlib -mno-sse -mno-sse2

$(BUILD)/libc.o: user/libc.c
	@mkdir -p $(BUILD)
	@echo "  CC   user/libc.c"
	@$(CC) $(USER_CFLAGS) -c user/libc.c -o $@

$(BUILD)/hello.elf: user/hello.c $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/hello.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/hello.c $(BUILD)/libc.o -o $@

$(BUILD)/shell.elf: user/shell.c $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/shell.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/shell.c $(BUILD)/libc.o -o $@

$(BUILD)/gui.elf: user/gui.c $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/gui.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/gui.c $(BUILD)/libc.o -o $@

$(BUILD)/ls.elf: user/ls.c $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/ls.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/ls.c $(BUILD)/libc.o -o $@

$(BUILD)/cat.elf: user/cat.c $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/cat.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/cat.c $(BUILD)/libc.o -o $@

$(BUILD)/memtest.elf: user/memtest.c $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/memtest.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/memtest.c $(BUILD)/libc.o -o $@

$(BUILD)/desktop.elf: user/desktop.c $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/desktop.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/desktop.c $(BUILD)/libc.o -o $@

# -----------------------------------------------------------------------------
#  Initrd (TAR) összerakása
# -----------------------------------------------------------------------------
$(INITRD_TAR): $(BUILD)/hello.elf $(BUILD)/shell.elf $(BUILD)/gui.elf $(BUILD)/ls.elf $(BUILD)/cat.elf $(BUILD)/memtest.elf $(BUILD)/desktop.elf
	@mkdir -p $(BUILD)
	@echo "  TAR  $@"
	@cp $(BUILD)/hello.elf initrd/hello.elf
	@cp $(BUILD)/shell.elf initrd/shell.elf
	@cp $(BUILD)/gui.elf initrd/gui.elf
	@cp $(BUILD)/ls.elf initrd/ls.elf
	@cp $(BUILD)/cat.elf initrd/cat.elf
	@cp $(BUILD)/memtest.elf initrd/memtest.elf
	@cp $(BUILD)/desktop.elf initrd/desktop.elf
	@cd initrd && tar -cf ../$@ .

# -----------------------------------------------------------------------------
#  Bootolható ISO image összerakása
# -----------------------------------------------------------------------------
$(ISO): $(BUILD)/$(KERNEL) $(INITRD_TAR) $(LIMINE_DIR)/limine limine.conf
	@echo "  ISO  $@"
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/boot/limine
	@mkdir -p $(ISO_ROOT)/EFI/BOOT
	@cp $(BUILD)/$(KERNEL)                  $(ISO_ROOT)/boot/
	@cp $(INITRD_TAR)                       $(ISO_ROOT)/boot/
	@cp limine.conf                         $(ISO_ROOT)/boot/limine/
	@cp $(LIMINE_DIR)/limine-bios.sys       $(ISO_ROOT)/boot/limine/
	@cp $(LIMINE_DIR)/limine-bios-cd.bin    $(ISO_ROOT)/boot/limine/
	@cp $(LIMINE_DIR)/limine-uefi-cd.bin    $(ISO_ROOT)/boot/limine/
	@cp $(LIMINE_DIR)/BOOTX64.EFI           $(ISO_ROOT)/EFI/BOOT/
	@xorriso -as mkisofs -quiet \
	    -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    $(ISO_ROOT) -o $@ 2>/dev/null
	@./$(LIMINE_DIR)/limine bios-install $@ >/dev/null 2>&1
	@echo "  DONE $@"

iso: $(ISO)

# -----------------------------------------------------------------------------
#  FAT32 lemezkép készítése (mtools — nem kell root)
#  64 MB-os, FAT32 formátumú, néhány teszt fájllal a gyökerében.
# -----------------------------------------------------------------------------
DISK_SIZE_MB := 64

$(DISK):
	@echo "  GEN  $@ ($(DISK_SIZE_MB)MB FAT32)"
	@dd if=/dev/zero of=$@ bs=1M count=$(DISK_SIZE_MB) status=none
	@mformat -i $@ -F -v REXOSDATA ::
	@mkdir -p $(BUILD)/diskroot
	@echo "Welcome to RexOS FAT32 partition!"               > $(BUILD)/diskroot/README.TXT
	@echo "This file lives on /mnt (FAT32 disk image)."    >> $(BUILD)/diskroot/README.TXT
	@echo ""                                                >> $(BUILD)/diskroot/README.TXT
	@echo "You can write any data to disk.img using mtools,">> $(BUILD)/diskroot/README.TXT
	@echo "and RexOS will be able to read it through /mnt." >> $(BUILD)/diskroot/README.TXT
	@echo "RexOS Notes"                                     > $(BUILD)/diskroot/NOTES.TXT
	@echo "==========="                                    >> $(BUILD)/diskroot/NOTES.TXT
	@echo "FAT32 driver: read-only, LFN supported."        >> $(BUILD)/diskroot/NOTES.TXT
	@echo "ATA driver:   PIO (LBA28), primary master."     >> $(BUILD)/diskroot/NOTES.TXT
	@printf "Lorem ipsum dolor sit amet. " > $(BUILD)/diskroot/HELLO.TXT
	@printf "Hello from FAT32, RexOS!\n"  >> $(BUILD)/diskroot/HELLO.TXT
	@mcopy -i $@ $(BUILD)/diskroot/README.TXT ::
	@mcopy -i $@ $(BUILD)/diskroot/NOTES.TXT ::
	@mcopy -i $@ $(BUILD)/diskroot/HELLO.TXT ::
	@mmd   -i $@ ::/DOCS 2>/dev/null || true
	@echo "Subdirectory test file." > $(BUILD)/diskroot/sub.txt
	@mcopy -i $@ $(BUILD)/diskroot/sub.txt ::/DOCS/sub.txt
	@echo "  DONE $@"

disk: $(DISK)

# -----------------------------------------------------------------------------
#  Futtatás QEMU-ban
# -----------------------------------------------------------------------------
# --- Modern q35 AHCI mód (alapértelmezett) ---
QEMU_FLAGS  := \
    -M q35 \
    -m 256M \
    -cdrom $(ISO) \
    -drive id=disk0,file=$(DISK),format=raw,if=none \
    -device ide-hd,bus=ide.0,drive=disk0 \
    -boot d \
    -serial stdio \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -no-reboot -no-shutdown

# --- Legacy PC mód (IDE PIO 0x1F0) ---
QEMU_FLAGS_LEGACY := \
    -M pc \
    -m 256M \
    -cdrom $(ISO) \
    -drive file=$(DISK),format=raw,if=ide,index=0,media=disk \
    -boot d \
    -serial stdio \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -no-reboot -no-shutdown

run: $(ISO) $(DISK)
	qemu-system-x86_64 $(QEMU_FLAGS)

run-legacy: $(ISO) $(DISK)
	qemu-system-x86_64 $(QEMU_FLAGS_LEGACY)

# --- Full modern mode: AHCI + xHCI USB keyboard/mouse ---
# Ez a legjobban közelíti egy valódi modern gép hardvere: nincs legacy IDE
# (csak az ide-hd, amit AHCI-n keresztül érünk el), és az összes input
# USB-n érkezik. Ha ez működik, a physical HW-n is jó esélyed van.
run-usb: $(ISO) $(DISK)
	qemu-system-x86_64 $(QEMU_FLAGS) \
	    -device qemu-xhci,id=xhci \
	    -device usb-kbd,bus=xhci.0 \
	    -device usb-mouse,bus=xhci.0

# UEFI mód - az OVMF firmware útvonal disztribúció-függő.
# CachyOS / Arch: /usr/share/edk2/x64/OVMF.4m.fd
OVMF        := /usr/share/edk2/x64/OVMF.4m.fd

run-uefi: $(ISO) $(DISK)
	qemu-system-x86_64 $(QEMU_FLAGS) -bios $(OVMF)

# -----------------------------------------------------------------------------
#  Tisztítás
# -----------------------------------------------------------------------------
clean:
	@rm -rf $(BUILD) $(ISO) $(DISK)
	@echo "  CLEAN"
