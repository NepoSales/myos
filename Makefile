# myOS build system
#
#   make          build the kernel binary (build/myos.bin)
#   make iso      build a bootable GRUB ISO (build/myos.iso)
#   make run      boot the kernel directly in QEMU
#   make run-iso  boot the ISO in QEMU
#   make clean    remove build output

CC      := gcc
AS      := nasm
QEMU    := qemu-system-i386

BUILD   := build
KERNEL  := $(BUILD)/myos.bin
ISO     := $(BUILD)/myos.iso

CFLAGS  := -m32 -std=gnu99 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -fno-omit-frame-pointer -mgeneral-regs-only -O2 -Wall -Wextra -Ikernel
ASFLAGS := -f elf32
LDFLAGS := -m32 -nostdlib -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -T kernel/linker.ld

C_SRCS   := $(shell find kernel -name '*.c')
ASM_SRCS := boot/boot.asm $(shell find kernel -name '*.asm')
OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
            $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRCS))
DEPS     := $(OBJS:.o=.d)

QEMU_FLAGS := -m 128M -serial stdio

.PHONY: all iso run run-iso clean

all: $(KERNEL)

$(KERNEL): $(OBJS) kernel/linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

iso: $(ISO)

$(ISO): $(KERNEL)
	@mkdir -p $(BUILD)/isodir/boot/grub
	cp $(KERNEL) $(BUILD)/isodir/boot/myos.bin
	printf 'set timeout=0\nset default=0\nmenuentry "myOS" {\n    multiboot /boot/myos.bin\n    boot\n}\n' \
	    > $(BUILD)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/isodir 2>/dev/null

run: $(KERNEL)
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL)

run-iso: $(ISO)
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO)

clean:
	rm -rf $(BUILD)

-include $(DEPS)
