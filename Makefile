# =====================================================
#   VAM Kernel Build System (with Tasking support)
# =====================================================

SHELL := /bin/bash

AS       = nasm
CC       = gcc
LD       = ld
OBJCOPY  = objcopy
GRUBMK   = grub-mkrescue

CFLAGS   = -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -O2 -Wall -Wextra \
           -Ikernel -Ikernel/mm -Ikernel/include -Ikernel/task
LDFLAGS  = -m elf_i386 -nostdlib

BUILD       = build
ISO_DIR     = iso_root
KERNEL_ELF  = $(BUILD)/kernel.elf
KERNEL_MAP  = $(BUILD)/kernel.map
FONT_PSF    = font/Cyr_a8x14.psf
FONT_OBJ    = $(BUILD)/font_psf.o
LINKER      = kernel/linker.ld

# =====================================================
#   Source Files
# =====================================================

KERNEL_C_SRC = \
	kernel/kernel.c \
	kernel/bootinfo.c \
	kernel/lfb.c \
	kernel/pmm.c \
	kernel/mm/vmm.c \
	kernel/idt.c \
	kernel/isr.c \
	kernel/pic.c \
	kernel/pit.c \
	kernel/serial.c \
	kernel/keyboard.c \
	kernel/gdt.c \
	kernel/tss.c \
	kernel/syscall.c \
	kernel/fb.c \
	kernel/psf.c \
	kernel/string.c \
	kernel/io.c \
	kernel/mm/kmalloc.c \
	kernel/panic/panic.c \
	kernel/tss_panic/df_tss.c \
	kernel/task/task.c  \
	kernel/usercode.c

KERNEL_ASM_SRC = \
	kernel/isr_asm.asm \
	kernel/isr_stub.asm \
	kernel/user_enter.asm \
	kernel/kernel_entry.asm \
	kernel/gdt_flush.asm \
	kernel/tss_flush.asm \
	kernel/idt_load.asm \
	kernel/tss_panic/df_task.asm

KERNEL_S_SRC = \
	kernel/usermode_trampoline.S\
	kernel/task/switch.S 
# =====================================================
#   Default Target
# =====================================================
all: $(BUILD) $(KERNEL_ELF) iso

$(BUILD):
	mkdir -p $(BUILD)

# =====================================================
#   Compilation Rules
# =====================================================

# C source
$(BUILD)/%.o: kernel/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly (.asm)
$(BUILD)/%.o: kernel/%.asm | $(BUILD)
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

# Assembly (.S)
$(BUILD)/%.o: kernel/%.S | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Font binary → object
$(FONT_OBJ): $(FONT_PSF) | $(BUILD)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@ \
		--redefine-sym _binary_font_Cyr_a8x14_psf_start=_binary_font_psf_start \
		--redefine-sym _binary_font_Cyr_a8x14_psf_end=_binary_font_psf_end \
		--rename-section .data=.rodata.font

# =====================================================
#   Object Linking
# =====================================================

KOBJ = \
	$(patsubst kernel/%.c,  $(BUILD)/%.o, $(KERNEL_C_SRC)) \
	$(patsubst kernel/%.asm,$(BUILD)/%.o, $(KERNEL_ASM_SRC)) \
	$(patsubst kernel/%.S,  $(BUILD)/%.o, $(KERNEL_S_SRC)) \
	$(FONT_OBJ)

$(KERNEL_ELF): $(KOBJ) $(LINKER)
	$(LD) $(LDFLAGS) -T $(LINKER) -Map $(KERNEL_MAP) -o $@ $(KOBJ)

# =====================================================
#   ISO Image Build
# =====================================================
iso: $(KERNEL_ELF)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	echo 'set timeout_style=menu'                 >  $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set timeout=5'                         >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set default=0'                         >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set gfxpayload=text'                   >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'terminal_output console'               >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'menuentry "VAM Kernel (multiboot2)" {' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '  multiboot2 /boot/kernel.elf'         >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '  boot'                                >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '}'                                     >> $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUBMK) -o $(BUILD)/os.iso $(ISO_DIR)
	@echo "✅ ISO built successfully: $(BUILD)/os.iso"
	@echo "🗺  Linker map: $(KERNEL_MAP)"

# =====================================================
#   Run & Debug
# =====================================================
run: iso
	qemu-system-i386 -cdrom $(BUILD)/os.iso -serial stdio -m 256M -vga vmware

debug: iso
	qemu-system-i386 -cdrom $(BUILD)/os.iso -serial stdio -no-reboot -no-shutdown \
		-monitor telnet:127.0.0.1:55555,server,nowait -vga vmware -m 256M \
		-d int,cpu_reset,guest_errors -M smm=off

# =====================================================
#   Verify / Clean
# =====================================================
verify: $(KERNEL_ELF)
	@echo "=== readelf -S ==="; readelf -S $(KERNEL_ELF) | sed -n '1,200p'
	@echo "=== objdump -h ==="; objdump -h $(KERNEL_ELF)
	@echo "=== multiboot2 header probe (grep) ==="
	@objdump -s -j .multiboot2 $(KERNEL_ELF) | head -n 40 || true
	@echo "=== first few map lines ==="; head -n 60 $(KERNEL_MAP) || true
	@echo "Tip: 확인할 심볼 → __text_lma, __kernel_high_start, __kernel_high_end, page_directory"

clean:
	rm -rf $(BUILD) $(ISO_DIR)

.PHONY: all clean run debug iso verify
