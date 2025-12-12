# =====================================================
#   VAM Kernel Build System (Limine/UEFI + BIOS)
# =====================================================

SHELL := /bin/bash

AS       = nasm
CC       = gcc
LD       = ld
OBJCOPY  = objcopy

CFLAGS = -m64 -mcmodel=kernel -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
         -O2 -Wall -Wextra -nostdlib -nostdinc \
         -Ikernel -Ikernel/mm -Ikernel/include -Ikernel/task -Ilimine \
         -DBIOS -DCOM_OUTPUT=0 -DFLANTERM_IN_FLANTERM \
		 -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-avx -mno-80387 -mno-red-zone \
		 -g -ggdb -fno-eliminate-unused-debug-types


LDFLAGS  = -m elf_x86_64 -nostdlib

BUILD       = build
ISO_DIR     = iso_root
LIMINE_DIR  = limine
KERNEL_ELF  = $(BUILD)/kernel.elf
KERNEL_MAP  = $(BUILD)/kernel.map
FONT_PSF    = font/Cyr_a8x14.psf
FONT_TTF    ?=
FONT_SIZE   ?= 16
FONT_EMBOLDEN ?= 0
FONT_CELL_W ?=
FONT_BIN    = $(BUILD)/font_gray.gfn
TTF2GFNT    = $(BUILD)/ttf2gfnt
TTF2GFNT_SRC= tools/ttf2gfnt.c
FONT_OBJ    = $(BUILD)/font_psf.o
LINKER      = kernel/linker.ld

# =====================================================
#   Source Files
# =====================================================

KERNEL_C_SRC  := $(shell find kernel -type f -name '*.c')
KERNEL_ASM_SRC := $(shell find kernel -type f -name '*.asm')
KERNEL_S_SRC  := $(shell find kernel -type f -name '*.S')

# =====================================================
#   Default
# =====================================================

all: $(BUILD) $(KERNEL_ELF) iso

$(BUILD):
	mkdir -p $(BUILD)

# =====================================================
#   Compile Objects
# =====================================================

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@


$(BUILD)/%.o: kernel/%.asm | $(BUILD)
	@mkdir -p $(dir $@)
	$(AS) -f elf64  $< -o $@


$(BUILD)/%.o: kernel/%.S | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# =====================================================
#   Font
# =====================================================

ifeq ($(strip $(FONT_TTF)),)
FONT_BLOB := $(FONT_PSF)
else
FONT_BLOB := $(FONT_BIN)
endif

FONT_SYM_PREFIX := _binary_$(subst .,_,$(subst /,_,$(FONT_BLOB)))

$(TTF2GFNT): $(TTF2GFNT_SRC) tools/stb_truetype.h | $(BUILD)
	$(CC) -O2 -std=c99 -Wall -Wextra -o $@ $< -lm

$(FONT_BIN): $(FONT_TTF) $(TTF2GFNT) | $(BUILD)
	$(TTF2GFNT) $(FONT_TTF) $(FONT_SIZE) $@ $(FONT_EMBOLDEN) $(FONT_CELL_W)

$(FONT_OBJ): $(FONT_BLOB) | $(BUILD)
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		$(FONT_BLOB) $@ \
		--redefine-sym $(FONT_SYM_PREFIX)_start=_binary_font_psf_start \
		--redefine-sym $(FONT_SYM_PREFIX)_end=_binary_font_psf_end \
		--rename-section .data=.rodata.font


# =====================================================
#   Link
# =====================================================

KOBJ = \
	$(patsubst kernel/%.c,  $(BUILD)/%.o, $(KERNEL_C_SRC)) \
	$(patsubst kernel/%.asm,$(BUILD)/%.o, $(KERNEL_ASM_SRC)) \
	$(patsubst kernel/%.S,  $(BUILD)/%.o, $(KERNEL_S_SRC)) \
	$(FONT_OBJ)

# Remove duplicate implementations we don't want to link twice
KOBJ := $(filter-out \
	$(BUILD)/limine.o \
	$(BUILD)/sys/pic.o \
	$(BUILD)/protos/chainload.o \
	$(BUILD)/sys/idt.s2.o \
	$(BUILD)/stubs.o \
, $(KOBJ))

$(KERNEL_ELF): $(KOBJ) $(LINKER)
	$(LD) $(LDFLAGS) -T $(LINKER) -Map $(KERNEL_MAP) -o $@ $(KOBJ)

# =====================================================
#   Limine ISO Build
# =====================================================

iso: $(KERNEL_ELF)
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)

	# Copy Limine bootloader files (from releases)
	#mkdir -p $(ISO_DIR)/limine
	cp $(LIMINE_DIR)/limine.sys           $(ISO_DIR)/
	cp $(LIMINE_DIR)/limine-bios.sys     $(ISO_DIR)/
	cp $(LIMINE_DIR)/limine-cd.bin       $(ISO_DIR)/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin  $(ISO_DIR)/

	# EFI executable
	mkdir -p $(ISO_DIR)/EFI/BOOT
	cp $(LIMINE_DIR)/BOOTX64.EFI          $(ISO_DIR)/EFI/BOOT/

	# Kernel
	mkdir -p $(ISO_DIR)/boot
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf

	# limine.conf
	cp limine.conf $(ISO_DIR)/limine.conf
	


	mkdir -p $(ISO_DIR)/boot
	cp $(ISO_DIR)/limine.conf $(ISO_DIR)/boot/limine.conf

	# Create ISO
	xorriso -as mkisofs \
		-R -J \
		-V "PARANOS" \
		-b limine-bios.sys \
			-no-emul-boot \
			-boot-load-size 4 \
			-boot-info-table \
		-eltorito-alt-boot \
		-e limine-uefi-cd.bin \
			-no-emul-boot \
		-isohybrid-mbr limine/limine-bios.sys \
		-partition_offset 16 \
		-append_partition 2 0xef limine/limine-uefi-cd.bin \
	-o build/os.iso \
	iso_root





	# Patch ISO with limine-install
	$(LIMINE_DIR)/limine bios-install $(abspath $(BUILD)/os.iso)

	@echo "====================================================="
	@echo "  Limine ISO built successfully: $(BUILD)/os.iso"
	@echo "====================================================="

# =====================================================
#   Run
# =====================================================

run: iso
	sudo qemu-system-x86_64 -m 1024M -machine q35 \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
		-drive if=pflash,format=raw,file=/usr/share/OVMF/OVMF_VARS_4M.fd \
		-device piix3-ide,id=ide \
		-drive id=hd0,file=disk.img,if=none,format=raw \
		-device ide-hd,drive=hd0,bus=ide.0,bootindex=2 \
		-drive id=cd0,file=build/os.iso,if=none,format=raw,media=cdrom \
		-device ide-cd,drive=cd0,bus=ide.1,bootindex=1 \
		-nic none \
		-boot order=d,menu=on -serial stdio -vga std -display sdl


# =====================================================
#   Utilities
# =====================================================


clean:
	rm -rf $(BUILD) $(ISO_DIR)

.PHONY: all clean run iso

link: $(KERNEL_ELF)

.PHONY: link
