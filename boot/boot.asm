; boot.asm - Multiboot entry point
;
; GRUB (or QEMU -kernel) loads the kernel at 1 MiB, switches the CPU to
; 32-bit protected mode and jumps to _start. At that point:
;   eax = multiboot magic (0x2BADB002)
;   ebx = physical address of the multiboot information structure

MBALIGN     equ  1 << 0              ; align loaded modules on page boundaries
MEMINFO     equ  1 << 1              ; ask the bootloader for a memory map
FLAGS       equ  MBALIGN | MEMINFO
MAGIC       equ  0x1BADB002
CHECKSUM    equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
global boot_stack_top
boot_stack_bottom:
    resb 16384                       ; 16 KiB stack for the boot/idle task
boot_stack_top:

section .text
global _start
extern kernel_main

_start:
    cli
    mov esp, boot_stack_top

    push ebx                         ; arg 2: multiboot info pointer
    push eax                         ; arg 1: multiboot magic
    call kernel_main

    ; kernel_main never returns, but just in case: halt forever
.hang:
    cli
    hlt
    jmp .hang
