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
# A nasm/xorriso lokálisan (~/.local/bin) is lehet telepítve (sudo nélküli
# gépeken forrásból) - vegyük fel a PATH-ba, ha létezik.
export PATH := $(HOME)/.local/bin:$(PATH)

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
#  BearSSL (TLS könyvtár) - third_party/bearssl/
#  Freestanding build a kernelünk flagjeivel. Statikus libként készül,
#  egyelőre a kerneltől függetlenül; később a user-space HTTPS kliens
#  (third_party/bearssl/inc/bearssl.h headerek) köti be.
# -----------------------------------------------------------------------------
BEARSSL_DIR   := third_party/bearssl
BEARSSL_BUILD := $(BUILD)/bearssl
BEARSSL_SRCS  := $(shell find $(BEARSSL_DIR)/src -name '*.c' 2>/dev/null)
BEARSSL_OBJS  := $(patsubst $(BEARSSL_DIR)/src/%.c,$(BEARSSL_BUILD)/%.o,$(BEARSSL_SRCS))
BEARSSL_LIB   := $(BEARSSL_BUILD)/libbearssl.a

# A kernel-CFLAGS-szal kompatibilis, de BearSSL-specifikus -D kapcsolók.
# Lásd third_party/bearssl/src/config.h - minden #ifndef-fel védett.
BEARSSL_CFLAGS := \
    -std=c17 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-PIC -fno-PIE \
    -m64 \
    -march=x86-64 \
    -mno-red-zone \
    -mno-mmx -mno-sse -mno-sse2 \
    -mcmodel=kernel \
    -O2 \
    -DBR_64=1 \
    -DBR_INT128=1 \
    -DBR_LE_UNALIGNED=1 \
    -DBR_SSE2=0 \
    -DBR_AES_X86NI=0 \
    -DBR_USE_URANDOM=0 \
    -DBR_USE_UNIX_TIME=0 \
    -DBR_RDRAND=0 \
    -I$(BEARSSL_DIR)/inc \
    -I$(BEARSSL_DIR)/src

$(BEARSSL_BUILD)/%.o: $(BEARSSL_DIR)/src/%.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	@$(CC) $(BEARSSL_CFLAGS) -c $< -o $@

$(BEARSSL_LIB): $(BEARSSL_OBJS)
	@mkdir -p $(dir $@)
	@echo "  AR   $@"
	@ar rcs $@ $(BEARSSL_OBJS)

bearssl: $(BEARSSL_LIB)
.PHONY: bearssl

# -----------------------------------------------------------------------------
#  BearSSL - userspace build (statikus lib a user/tls.c-hez)
#  Ugyanaz a forrás, de userspace flag-ekkel (-fpie, nincs -mcmodel=kernel),
#  hogy a PIE userspace ELF-be linkelhető legyen.
# -----------------------------------------------------------------------------
BEARSSL_USER_BUILD := $(BUILD)/bearssl_user
BEARSSL_USER_OBJS  := $(patsubst $(BEARSSL_DIR)/src/%.c,$(BEARSSL_USER_BUILD)/%.o,$(BEARSSL_SRCS))
BEARSSL_USER_LIB   := $(BEARSSL_USER_BUILD)/libbearssl_user.a

BEARSSL_USER_CFLAGS := \
    -std=c17 \
    -ffreestanding \
    -fno-stack-protector \
    -fpie \
    -m64 \
    -march=x86-64 \
    -mno-red-zone \
    -mno-mmx -mno-sse -mno-sse2 \
    -O2 \
    -DBR_64=1 \
    -DBR_INT128=1 \
    -DBR_LE_UNALIGNED=1 \
    -DBR_SSE2=0 \
    -DBR_AES_X86NI=0 \
    -DBR_USE_URANDOM=0 \
    -DBR_USE_UNIX_TIME=0 \
    -DBR_RDRAND=0 \
    -I$(BEARSSL_DIR)/inc \
    -I$(BEARSSL_DIR)/src

$(BEARSSL_USER_BUILD)/%.o: $(BEARSSL_DIR)/src/%.c
	@mkdir -p $(dir $@)
	@echo "  CC   (user) $<"
	@$(CC) $(BEARSSL_USER_CFLAGS) -c $< -o $@

$(BEARSSL_USER_LIB): $(BEARSSL_USER_OBJS)
	@mkdir -p $(dir $@)
	@echo "  AR   $@"
	@ar rcs $@ $(BEARSSL_USER_OBJS)

bearssl-user: $(BEARSSL_USER_LIB)
.PHONY: bearssl-user

# -----------------------------------------------------------------------------
#  Kernel ELF linkelés
# -----------------------------------------------------------------------------
$(BUILD)/$(KERNEL): $(OBJS) $(KDIR)/linker.ld
	@mkdir -p $(dir $@)
	@echo "  LD   $@"
	@$(LD) $(LDFLAGS) $(OBJS) -o $@

# -----------------------------------------------------------------------------
#  Object fordítás
#  Minden kernel .o függ az összes kernel headertől: ha egy struct layout
#  változik (pl. task_t), MINDEN érintett fordítási egység újraforduljon,
#  különben a stale .o-k régi mező-offsetekkel olvasnák a structokat.
# -----------------------------------------------------------------------------
KERNEL_HDRS := $(shell find $(KDIR) -name '*.h' 2>/dev/null)

$(BUILD)/%.c.o: $(KDIR)/%.c $(KERNEL_HDRS)
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
# SSE engedélyezve: a scheduler menti az FPU/SSE állapotot (fxsave/fxrstor),
# és az x86_64 SysV ABI amúgy is XMM-ben adja át a float/double argumentumokat.
USER_CFLAGS = -Wall -Wextra -O2 -ffreestanding -fno-stack-protector -fno-pic -fpie -mno-red-zone -nostdlib

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

$(BUILD)/fptest.elf: user/fptest.c user/libc.h $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/fptest.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/fptest.c $(BUILD)/libc.o -o $@

$(BUILD)/fpspin.elf: user/fpspin.c user/libc.h $(BUILD)/libc.o
	@mkdir -p $(BUILD)
	@echo "  CC   user/fpspin.c"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/fpspin.c $(BUILD)/libc.o -o $@

# --- TLS layer (BearSSL userspace lib-re épül) ---
USER_TLS_CFLAGS := $(USER_CFLAGS) -I$(BEARSSL_DIR)/inc

$(BUILD)/tls.o: user/tls.c user/tls.h user/libc.h $(BEARSSL_USER_LIB)
	@mkdir -p $(BUILD)
	@echo "  CC   user/tls.c"
	@$(CC) $(USER_TLS_CFLAGS) -c user/tls.c -o $@

$(BUILD)/ca_bundle.o: user/ca_bundle.c $(BEARSSL_USER_LIB)
	@mkdir -p $(BUILD)
	@echo "  CC   user/ca_bundle.c"
	@$(CC) $(USER_TLS_CFLAGS) -c user/ca_bundle.c -o $@

# TLS skeleton fordítás-teszt: nem linkeljük még appba, csak biztosítjuk
# hogy a userspace BearSSL + tls.c + ca_bundle.c forrásilag jó.
tls-check: $(BUILD)/tls.o $(BUILD)/ca_bundle.o
.PHONY: tls-check

$(BUILD)/http.o: user/http.c user/http.h user/libc.h user/tls.h user/inflate.h
	@mkdir -p $(BUILD)
	@echo "  CC   user/http.c (with TLS)"
	@$(CC) $(USER_TLS_CFLAGS) -c user/http.c -o $@

# --- Hálózati end-to-end önteszt (headless QEMU teszthez) ---
$(BUILD)/nettest.elf: user/nettest.c user/http.h user/libc.h \
                      $(BUILD)/libc.o $(BUILD)/http.o $(BUILD)/tls.o \
                      $(BUILD)/ca_bundle.o $(BEARSSL_USER_LIB)
	@mkdir -p $(BUILD)
	@echo "  CC   user/nettest.c"
	@$(CC) $(USER_TLS_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none \
	    user/nettest.c \
	    $(BUILD)/libc.o $(BUILD)/http.o $(BUILD)/tls.o $(BUILD)/ca_bundle.o \
	    $(BEARSSL_USER_LIB) \
	    -o $@

# --- DEFLATE / gzip dekompresszió (puff-alapú, plain C, nincs float) ---
$(BUILD)/inflate.o: user/inflate.c user/inflate.h user/libc.h
	@mkdir -p $(BUILD)
	@echo "  CC   user/inflate.c"
	@$(CC) $(USER_CFLAGS) -c user/inflate.c -o $@

# --- Font subsystem (stb_truetype + DejaVu Sans Mono) ---
# A font kódhoz SSE/SSE2 KELL (x86-64 ABI float ABI), és kifejezetten engedjük az
# -O2-t (stb_truetype rasterizer hot path). A USER_CFLAGS -mno-sse-jét levesszük.
USER_FONT_CFLAGS := $(filter-out -mno-sse -mno-sse2,$(USER_CFLAGS)) \
    -msse -msse2 \
    -Wno-unused-function \
    -Wno-unused-but-set-variable \
    -Wno-sign-compare \
    -Wno-missing-field-initializers \
    -Wno-maybe-uninitialized

$(BUILD)/math_min.o: user/math_min.c
	@mkdir -p $(BUILD)
	@echo "  CC   user/math_min.c"
	@$(CC) $(USER_FONT_CFLAGS) -c user/math_min.c -o $@

$(BUILD)/font_stb.o: user/font_stb.c third_party/stb/stb_truetype.h user/libc.h
	@mkdir -p $(BUILD)
	@echo "  CC   user/font_stb.c (stb_truetype)"
	@$(CC) $(USER_FONT_CFLAGS) -c user/font_stb.c -o $@

$(BUILD)/font.o: user/font.c user/font.h user/libc.h
	@mkdir -p $(BUILD)
	@echo "  CC   user/font.c"
	@$(CC) $(USER_FONT_CFLAGS) -c user/font.c -o $@

# Font binary (DejaVu Sans Mono TTF) közvetlen object-é alakítása linkerrel.
# Ez sokkal gyorsabb mint xxd-generated C array (~28000 sor).
$(BUILD)/font_data.o: third_party/fonts/dejavu_mono.ttf
	@mkdir -p $(BUILD)
	@echo "  BIN  $< -> $@"
	@cd third_party/fonts && $(LD) -r -b binary -o $(abspath $@) dejavu_mono.ttf

# --- Modular desktop environment (user/desktop/*.c) ---
DESKTOP_SRCS := user/desktop/main.c user/desktop/render.c user/desktop/wm.c \
                user/desktop/taskbar.c user/desktop/icons.c user/desktop/apps.c \
                user/desktop/app_browser.c
DESKTOP_OBJS := $(DESKTOP_SRCS:user/desktop/%.c=$(BUILD)/desktop_%.o)

$(BUILD)/desktop_%.o: user/desktop/%.c user/desktop/desktop.h user/desktop/theme.h \
                      user/libc.h user/font.h user/http.h
	@mkdir -p $(BUILD)
	@echo "  CC   $<"
	@$(CC) $(USER_FONT_CFLAGS) -I$(BEARSSL_DIR)/inc -Iuser -c $< -o $@

$(BUILD)/desktop.elf: $(DESKTOP_OBJS) \
                      $(BUILD)/libc.o \
                      $(BUILD)/http.o $(BUILD)/inflate.o $(BUILD)/tls.o $(BUILD)/ca_bundle.o \
                      $(BUILD)/font.o $(BUILD)/font_stb.o $(BUILD)/math_min.o $(BUILD)/font_data.o \
                      $(BEARSSL_USER_LIB)
	@mkdir -p $(BUILD)
	@echo "  LD   $@ (RexOS Desktop)"
	@$(CC) $(USER_FONT_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none \
	    $(DESKTOP_OBJS) \
	    $(BUILD)/libc.o \
	    $(BUILD)/http.o $(BUILD)/inflate.o $(BUILD)/tls.o $(BUILD)/ca_bundle.o \
	    $(BUILD)/font.o $(BUILD)/font_stb.o $(BUILD)/math_min.o $(BUILD)/font_data.o \
	    $(BEARSSL_USER_LIB) \
	    -o $@

# -----------------------------------------------------------------------------
#  Initrd (TAR) összerakása
# -----------------------------------------------------------------------------
# NETTEST=1 esetén a hálózati önteszt is bekerül az initrd-be, és a kernel
# boot után automatikusan elindítja (lásd kmain.c). Normál buildben nincs ott.
ifeq ($(NETTEST),1)
INITRD_NETTEST := $(BUILD)/nettest.elf
endif

$(INITRD_TAR): $(BUILD)/hello.elf $(BUILD)/shell.elf $(BUILD)/gui.elf $(BUILD)/ls.elf $(BUILD)/cat.elf $(BUILD)/memtest.elf $(BUILD)/fptest.elf $(BUILD)/fpspin.elf $(BUILD)/desktop.elf $(BUILD)/netsurf.elf $(INITRD_NETTEST)
	@mkdir -p $(BUILD)
	@echo "  TAR  $@"
	@cp $(BUILD)/hello.elf initrd/hello.elf
	@cp $(BUILD)/shell.elf initrd/shell.elf
	@cp $(BUILD)/gui.elf initrd/gui.elf
	@cp $(BUILD)/ls.elf initrd/ls.elf
	@cp $(BUILD)/cat.elf initrd/cat.elf
	@cp $(BUILD)/memtest.elf initrd/memtest.elf
	@cp $(BUILD)/fptest.elf initrd/fptest.elf
	@cp $(BUILD)/fpspin.elf initrd/fpspin.elf
	@cp $(BUILD)/desktop.elf initrd/desktop.elf
	@cp $(BUILD)/netsurf.elf initrd/netsurf.elf
	$(call NETSURF_RES_COPY,initrd)
	@printf 'enable_javascript:1\n' > initrd/netsurf/Choices
	@rm -f initrd/nettest.elf
ifeq ($(NETTEST),1)
	@cp $(BUILD)/nettest.elf initrd/nettest.elf
endif
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
#  FAT32 lemezkép készítése (mtools - nem kell root)
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
# Split the QEMU flags into small variables instead of line-continuation lists.
# That keeps make from parsing a following flag as a top-level statement if a
# continuation backslash picks up stray whitespace ("missing separator").
QEMU_MEMORY_FLAGS := -m 1G
QEMU_BOOT_FLAGS   := -cdrom $(ISO) -boot d
QEMU_SERIAL_FLAGS := -serial stdio
QEMU_NET_FLAGS    := -netdev user,id=net0 -device e1000,netdev=net0
QEMU_EXIT_FLAGS   := -no-reboot -no-shutdown

# --- Modern q35 AHCI mód (alapértelmezett) ---
QEMU_MACHINE_FLAGS := -M q35
QEMU_DISK_FLAGS    := -drive id=disk0,file=$(DISK),format=raw,if=none -device ide-hd,bus=ide.0,drive=disk0
QEMU_FLAGS         := $(QEMU_MACHINE_FLAGS) $(QEMU_MEMORY_FLAGS) $(QEMU_BOOT_FLAGS) $(QEMU_DISK_FLAGS) $(QEMU_SERIAL_FLAGS) $(QEMU_NET_FLAGS) $(QEMU_EXIT_FLAGS)

# --- Legacy PC mód (IDE PIO 0x1F0) ---
QEMU_LEGACY_MACHINE_FLAGS := -M pc
QEMU_LEGACY_DISK_FLAGS    := -drive file=$(DISK),format=raw,if=ide,index=0,media=disk
QEMU_FLAGS_LEGACY         := $(QEMU_LEGACY_MACHINE_FLAGS) $(QEMU_MEMORY_FLAGS) $(QEMU_BOOT_FLAGS) $(QEMU_LEGACY_DISK_FLAGS) $(QEMU_SERIAL_FLAGS) $(QEMU_NET_FLAGS) $(QEMU_EXIT_FLAGS)

run: $(ISO) $(DISK)
	qemu-system-x86_64 $(QEMU_FLAGS)

# -----------------------------------------------------------------------------
#  Newlib-es userspace appok (cross toolchain: tools/build-toolchain.sh)
#  Explicit -nostdlib link + --start-group, hogy a newlib libc és a RexOS
#  syscall glue (librexos.a) kölcsönös függőségei felolódjanak.
# -----------------------------------------------------------------------------
TC_CC := toolchain/bin/x86_64-elf-gcc
TC_AR := toolchain/bin/x86_64-elf-ar
NEWLIB_BUILD  := $(BUILD)/newlib
NEWLIB_CFLAGS := -O2 -g -Wall -Wextra

$(NEWLIB_BUILD)/crt0.o: user/newlib-glue/crt0.S
	@mkdir -p $(NEWLIB_BUILD)
	@echo "  AS   $<"
	@$(TC_CC) -c $< -o $@

$(NEWLIB_BUILD)/syscalls.o: user/newlib-glue/syscalls.c user/newlib-glue/rexos_syscall.h
	@mkdir -p $(NEWLIB_BUILD)
	@echo "  CC   $<"
	@$(TC_CC) $(NEWLIB_CFLAGS) -c $< -o $@

$(NEWLIB_BUILD)/librexos.a: $(NEWLIB_BUILD)/syscalls.o
	@echo "  AR   $@"
	@$(TC_AR) rcs $@ $^

# $(1) = objektumok, $(2) = extra libek (pl. -lpng16 -ljpeg -lz)
define NEWLIB_LINK
	@echo "  LD   $@"
	@$(TC_CC) -nostdlib -static -Wl,--build-id=none -Wl,-e,_start \
	    $(NEWLIB_BUILD)/crt0.o $(1) \
	    -Wl,--start-group $(NEWLIB_BUILD)/librexos.a $(2) -lc -lm -lgcc -Wl,--end-group \
	    -o $@
endef

$(BUILD)/hello_newlib.elf: user/newlib-glue/hello_newlib.c $(NEWLIB_BUILD)/crt0.o $(NEWLIB_BUILD)/librexos.a
	@echo "  CC   $<"
	@$(TC_CC) $(NEWLIB_CFLAGS) -c $< -o $(NEWLIB_BUILD)/hello_newlib.o
	$(call NEWLIB_LINK,$(NEWLIB_BUILD)/hello_newlib.o)

$(BUILD)/heaptest.elf: user/newlib-glue/heaptest.c $(NEWLIB_BUILD)/crt0.o $(NEWLIB_BUILD)/librexos.a
	@echo "  CC   $<"
	@$(TC_CC) $(NEWLIB_CFLAGS) -c $< -o $(NEWLIB_BUILD)/heaptest.o
	$(call NEWLIB_LINK,$(NEWLIB_BUILD)/heaptest.o)

$(BUILD)/imgview.elf: user/newlib-glue/imgview.c user/newlib-glue/rexos_syscall.h $(NEWLIB_BUILD)/crt0.o $(NEWLIB_BUILD)/librexos.a
	@echo "  CC   $<"
	@$(TC_CC) $(NEWLIB_CFLAGS) -c $< -o $(NEWLIB_BUILD)/imgview.o
	$(call NEWLIB_LINK,$(NEWLIB_BUILD)/imgview.o,-lpng16 -ljpeg -lz)

# -----------------------------------------------------------------------------
#  Automata tesztek (headless QEMU, soros kimenet, PASS/FAIL grep)
#  Generikus minta: $(BUILD)/initrd_<nev>.tar -> run-test-<nev>
#  A tesztelendő app desktop.elf néven kerül az initrd-be, így bootkor indul.
# -----------------------------------------------------------------------------
$(BUILD)/initrd_fputest.tar: $(BUILD)/fptest.elf $(BUILD)/fpspin.elf $(BUILD)/hello.elf
	@echo "  TAR  $@"
	@rm -rf $(BUILD)/initrd_fputest
	@mkdir -p $(BUILD)/initrd_fputest
	@cp $(BUILD)/fptest.elf $(BUILD)/initrd_fputest/desktop.elf
	@cp $(BUILD)/fpspin.elf $(BUILD)/initrd_fputest/fpspin.elf
	@cp $(BUILD)/hello.elf  $(BUILD)/initrd_fputest/hello.elf
	@cd $(BUILD)/initrd_fputest && tar -cf ../initrd_fputest.tar .

$(BUILD)/initrd_heaptest.tar: $(BUILD)/heaptest.elf
	@echo "  TAR  $@"
	@rm -rf $(BUILD)/initrd_heaptest
	@mkdir -p $(BUILD)/initrd_heaptest
	@cp $(BUILD)/heaptest.elf $(BUILD)/initrd_heaptest/desktop.elf
	@cd $(BUILD)/initrd_heaptest && tar -cf ../initrd_heaptest.tar .

$(BUILD)/initrd_newlibtest.tar: $(BUILD)/hello_newlib.elf $(BUILD)/hello.elf
	@echo "  TAR  $@"
	@rm -rf $(BUILD)/initrd_newlibtest
	@mkdir -p $(BUILD)/initrd_newlibtest
	@cp $(BUILD)/hello_newlib.elf $(BUILD)/initrd_newlibtest/desktop.elf
	@cp $(BUILD)/hello.elf        $(BUILD)/initrd_newlibtest/hello.elf
	@cd $(BUILD)/initrd_newlibtest && tar -cf ../initrd_newlibtest.tar .

# -----------------------------------------------------------------------------
#  NetSurf: a tools/build-netsurf.sh által fordított bináris + erőforrások
#  becsomagolása. A tarfs lapos, ezért a resource fájlok "netsurf/..." néven
#  kerülnek a tarba (a vfs_lookup exact-match fallbackje kezeli).
# -----------------------------------------------------------------------------
NSFB_BIN := toolchain/ports-src/netsurf-all-3.11/netsurf/nsfb
NSFB_RES := toolchain/ports-src/netsurf-all-3.11/netsurf/frontends/framebuffer/res

$(BUILD)/netsurf.elf: $(NSFB_BIN)
	@echo "  STRIP $@"
	@toolchain/bin/x86_64-elf-strip -o $@ $<

define NETSURF_RES_COPY
	@mkdir -p $(1)/netsurf
	@cp $(NSFB_RES)/en/Messages     $(1)/netsurf/Messages
	@cp $(NSFB_RES)/default.css     $(1)/netsurf/default.css
	@cp $(NSFB_RES)/internal.css    $(1)/netsurf/internal.css
	@cp $(NSFB_RES)/quirks.css      $(1)/netsurf/quirks.css
	@cp $(NSFB_RES)/adblock.css     $(1)/netsurf/adblock.css
	@cp $(NSFB_RES)/en/welcome.html $(1)/netsurf/welcome.html
	@cp $(NSFB_RES)/en/credits.html $(1)/netsurf/credits.html
	@cp $(NSFB_RES)/en/licence.html $(1)/netsurf/licence.html
	@cp $(NSFB_RES)/netsurf.png     $(1)/netsurf/netsurf.png
	@cp $(NSFB_RES)/favicon.png     $(1)/netsurf/favicon.png
endef

$(BUILD)/initrd_nstest.tar: $(BUILD)/netsurf.elf $(BUILD)/test.png
	@echo "  TAR  $@"
	@rm -rf $(BUILD)/initrd_nstest
	@mkdir -p $(BUILD)/initrd_nstest
	@cp $(BUILD)/netsurf.elf $(BUILD)/initrd_nstest/desktop.elf
	$(call NETSURF_RES_COPY,$(BUILD)/initrd_nstest)
	@cp $(BUILD)/test.png $(BUILD)/initrd_nstest/netsurf/test.png
	@printf '<html><head><title>RexOS teszt</title><style>body{background:#dfe;font-family:sans-serif} h1{color:#06c} .box{border:2px solid #06c;padding:8px;width:300px}</style></head><body><h1>NetSurf fut a RexOS-en!</h1><div class="box">CSS layout teszt doboz<br><img src="test.png"></div></body></html>' > $(BUILD)/initrd_nstest/netsurf/test.html
	@cd $(BUILD)/initrd_nstest && tar -cf ../initrd_nstest.tar .

# JavaScript teszt: helyi HTML DOM-manipuláló scripttel
$(BUILD)/initrd_nsjs.tar: $(BUILD)/netsurf.elf
	@echo "  TAR  $@"
	@rm -rf $(BUILD)/initrd_nsjs
	@mkdir -p $(BUILD)/initrd_nsjs
	@cp $(BUILD)/netsurf.elf $(BUILD)/initrd_nsjs/desktop.elf
	$(call NETSURF_RES_COPY,$(BUILD)/initrd_nsjs)
	@printf 'homepage_url:file:///netsurf/jstest.html\nenable_javascript:1\n' > $(BUILD)/initrd_nsjs/netsurf/Choices
	@printf '<html><head><title>JS teszt</title><style>#out{border:3px solid #c00;padding:10px;font-size:20px;background:#fee}</style></head><body><h1>RexOS JavaScript teszt</h1><div id="out">JS NEM FUT (statikus tartalom)</div><script>var d=document.getElementById("out");d.textContent="JS FUT! 6*7="+(6*7)+" | UA: "+navigator.userAgent;d.style.background="#efe";d.style.borderColor="#0a0";var p=document.createElement("p");p.textContent="Ezt a bekezdest JavaScript hozta letre (DOM createElement).";document.body.appendChild(p);</script></body></html>' > $(BUILD)/initrd_nsjs/netsurf/jstest.html
	@cd $(BUILD)/initrd_nsjs && tar -cf ../initrd_nsjs.tar .

# Élő hálózati teszt: a homepage http://example.com (QEMU user-mode net)
$(BUILD)/nsspawn.elf: user/nsspawn.c $(BUILD)/libc.o
	@echo "  CC   $<"
	@$(CC) $(USER_CFLAGS) -static -Wl,-e,_start -Wl,--build-id=none user/nsspawn.c $(BUILD)/libc.o -o $@

# A desktop valodi utvonala: spawn("netsurf.elf") + waitpid, GUI nelkul.
$(BUILD)/initrd_nsspawn.tar: $(BUILD)/netsurf.elf $(BUILD)/nsspawn.elf
	@echo "  TAR  $@"
	@rm -rf $(BUILD)/initrd_nsspawn
	@mkdir -p $(BUILD)/initrd_nsspawn
	@cp $(BUILD)/nsspawn.elf  $(BUILD)/initrd_nsspawn/desktop.elf
	@cp $(BUILD)/netsurf.elf  $(BUILD)/initrd_nsspawn/netsurf.elf
	$(call NETSURF_RES_COPY,$(BUILD)/initrd_nsspawn)
	@printf 'homepage_url:https://en.wikipedia.org/wiki/Operating_system\n' > $(BUILD)/initrd_nsspawn/netsurf/Choices
	@cd $(BUILD)/initrd_nsspawn && tar -cf ../initrd_nsspawn.tar .

# Merőpad: a testbench/ könyvtárat a hoston egy HTTP szerver szolgálja ki
# (QEMU slirp: a host = 10.0.2.2), a NetSurf homepage-e erre mutat.
# Használat: tools/bench.sh <oldal>   (pl. tools/bench.sh images)
BENCH_URL ?= http://10.0.2.2:8099/index.html
$(BUILD)/initrd_bench.tar: $(BUILD)/netsurf.elf
	@echo "  TAR  $@  ($(BENCH_URL))"
	@rm -rf $(BUILD)/initrd_bench
	@mkdir -p $(BUILD)/initrd_bench
	@cp $(BUILD)/netsurf.elf $(BUILD)/initrd_bench/desktop.elf
	$(call NETSURF_RES_COPY,$(BUILD)/initrd_bench)
	@printf 'homepage_url:$(BENCH_URL)\nenable_javascript:1\n' > $(BUILD)/initrd_bench/netsurf/Choices
	@cd $(BUILD)/initrd_bench && tar -cf ../initrd_bench.tar .

$(BUILD)/initrd_nsnet.tar: $(BUILD)/netsurf.elf
	@echo "  TAR  $@"
	@rm -rf $(BUILD)/initrd_nsnet
	@mkdir -p $(BUILD)/initrd_nsnet
	@cp $(BUILD)/netsurf.elf $(BUILD)/initrd_nsnet/desktop.elf
	$(call NETSURF_RES_COPY,$(BUILD)/initrd_nsnet)
	@printf 'homepage_url:https://en.wikipedia.org/wiki/Operating_system\n' > $(BUILD)/initrd_nsnet/netsurf/Choices
	@cd $(BUILD)/initrd_nsnet && tar -cf ../initrd_nsnet.tar .

# Teszt képek az imgview-hoz (ImageMagick): bal felső sarok = tiszta szín
$(BUILD)/test.png:
	@echo "  GEN  $@"
	@magick -size 200x160 gradient:red-yellow $@

$(BUILD)/test.jpg:
	@echo "  GEN  $@"
	@magick -size 200x160 gradient:blue-white -quality 90 $@

$(BUILD)/initrd_imgtest.tar: $(BUILD)/imgview.elf $(BUILD)/test.png $(BUILD)/test.jpg
	@echo "  TAR  $@"
	@rm -rf $(BUILD)/initrd_imgtest
	@mkdir -p $(BUILD)/initrd_imgtest
	@cp $(BUILD)/imgview.elf $(BUILD)/initrd_imgtest/desktop.elf
	@cp $(BUILD)/test.png    $(BUILD)/initrd_imgtest/test.png
	@cp $(BUILD)/test.jpg    $(BUILD)/initrd_imgtest/test.jpg
	@cd $(BUILD)/initrd_imgtest && tar -cf ../initrd_imgtest.tar .

$(BUILD)/rexos-test-%.iso: $(BUILD)/$(KERNEL) $(BUILD)/initrd_%.tar $(LIMINE_DIR)/limine limine.conf
	@echo "  ISO  $@"
	@rm -rf $(BUILD)/iso_root_test_$*
	@mkdir -p $(BUILD)/iso_root_test_$*/boot/limine
	@mkdir -p $(BUILD)/iso_root_test_$*/EFI/BOOT
	@cp $(BUILD)/$(KERNEL)                  $(BUILD)/iso_root_test_$*/boot/
	@cp $(BUILD)/initrd_$*.tar              $(BUILD)/iso_root_test_$*/boot/initrd.tar
	@cp limine.conf                         $(BUILD)/iso_root_test_$*/boot/limine/
	@cp $(LIMINE_DIR)/limine-bios.sys       $(BUILD)/iso_root_test_$*/boot/limine/
	@cp $(LIMINE_DIR)/limine-bios-cd.bin    $(BUILD)/iso_root_test_$*/boot/limine/
	@cp $(LIMINE_DIR)/limine-uefi-cd.bin    $(BUILD)/iso_root_test_$*/boot/limine/
	@cp $(LIMINE_DIR)/BOOTX64.EFI           $(BUILD)/iso_root_test_$*/EFI/BOOT/
	@xorriso -as mkisofs -quiet \
	    -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    $(BUILD)/iso_root_test_$* -o $@ 2>/dev/null
	@./$(LIMINE_DIR)/limine bios-install $@ >/dev/null 2>&1

run-test-%: $(BUILD)/rexos-test-%.iso $(DISK)
	@echo "  TEST $* (headless, max 60s)"
	@timeout 60 qemu-system-x86_64 $(QEMU_MACHINE_FLAGS) $(QEMU_MEMORY_FLAGS) \
	    -cdrom $(BUILD)/rexos-test-$*.iso -boot d $(QEMU_DISK_FLAGS) \
	    -display none -serial stdio $(QEMU_NET_FLAGS) -no-reboot \
	    | tee $(BUILD)/test_$*.log || true
	@grep -q "MINDEN TESZT SIKERES" $(BUILD)/test_$*.log \
	    && echo "== TEST $* PASS ==" \
	    || (echo "== TEST $* FAIL =="; exit 1)

# A nsspawn teszt sosem "fejezodik be" (a NetSurf addig fut, amig ki nem lepnek
# belole), ezert nem a sikersztringet keressuk: az a jo eset, ha a gyerek taszk
# elindult es a futas alatt EGYETLEN CPU exception sem tortent.
run-test-nsspawn: $(BUILD)/rexos-test-nsspawn.iso $(DISK)
	@echo "  TEST nsspawn (headless, max 120s)"
	@timeout 120 qemu-system-x86_64 $(QEMU_MACHINE_FLAGS) $(QEMU_MEMORY_FLAGS) \
	    -cdrom $(BUILD)/rexos-test-nsspawn.iso -boot d $(QEMU_DISK_FLAGS) \
	    -display none -serial stdio $(QEMU_NET_FLAGS) -no-reboot \
	    > $(BUILD)/test_nsspawn.log 2>&1 || true
	@grep -q "task\[3\] 'netsurf.elf' created" $(BUILD)/test_nsspawn.log \
	    || (echo "== TEST nsspawn FAIL (a netsurf.elf el sem indult) =="; exit 1)
	@! grep -q "CPU EXCEPTION" $(BUILD)/test_nsspawn.log \
	    || (echo "== TEST nsspawn FAIL (CPU exception) =="; \
	        grep -A16 "CPU EXCEPTION" $(BUILD)/test_nsspawn.log; exit 1)
	@echo "== TEST nsspawn PASS ($$(grep -c '\[fetch\] GET' $(BUILD)/test_nsspawn.log) letoltes, 0 exception) =="

# DOM-stressz: nagy oldal + képek + folyamatos JS-módosítás, közben gépelés.
# Ez a dobozfa-újraépítés őre (lásd tools/stress-dom.py).
run-test-domstress:
	@echo "  TEST domstress (headless, ~60s)"
	@python3 tools/stress-dom.py big 35

run-fputest: run-test-fputest
run-newlibtest: run-test-newlibtest
run-heaptest: run-test-heaptest

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
