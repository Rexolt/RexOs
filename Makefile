# =============================================================================
#  Rex OS - Top-level Makefile
#  Phase 1: "Rex is awakening..."
# =============================================================================

KERNEL      := rexos.elf
ISO         := rexos.iso

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
#  Futtatás QEMU-ban
# -----------------------------------------------------------------------------
QEMU_FLAGS  := \
    -M q35 \
    -m 256M \
    -cdrom $(ISO) \
    -boot d \
    -serial stdio \
    -no-reboot -no-shutdown

run: $(ISO)
	qemu-system-x86_64 $(QEMU_FLAGS)

# UEFI mód - az OVMF firmware útvonal disztribúció-függő.
# CachyOS / Arch: /usr/share/edk2/x64/OVMF.4m.fd
OVMF        := /usr/share/edk2/x64/OVMF.4m.fd

run-uefi: $(ISO)
	qemu-system-x86_64 $(QEMU_FLAGS) -bios $(OVMF)

# -----------------------------------------------------------------------------
#  Tisztítás
# -----------------------------------------------------------------------------
clean:
	@rm -rf $(BUILD) $(ISO)
	@echo "  CLEAN"
