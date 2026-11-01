; isr.asm - interrupt entry stubs
;
; Every interrupt vector we handle gets a tiny stub that pushes a uniform
; frame (error code + vector number) and jumps to isr_common. isr_common
; saves all registers, calls the C dispatcher, and then switches to whatever
; stack pointer the dispatcher returned. That return value is how the
; scheduler performs a context switch: it hands back the saved frame of a
; different task, and the final iret resumes that task instead.

extern interrupt_dispatch

%macro ISR_NOERR 1
isr%1:
    push dword 0                ; dummy error code
    push dword %1               ; vector number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr%1:                          ; the CPU already pushed an error code
    push dword %1
    jmp isr_common
%endmacro

section .text

; CPU exceptions 0-31 (8, 10-14, 17, 21, 29, 30 push an error code)
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

; hardware IRQs 0-15, remapped to vectors 32-47
%assign i 32
%rep 16
ISR_NOERR i
%assign i i+1
%endrep

; software interrupt for voluntary context switches
ISR_NOERR 48

isr_common:
    pusha                       ; eax, ecx, edx, ebx, esp, ebp, esi, edi
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10                ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; argument: regs_t* of the interrupted task
    call interrupt_dispatch     ; returns the stack pointer to resume
    mov esp, eax                ; (possibly a different task: context switch)

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                  ; drop vector number and error code
    iret

; table of stub addresses, used by idt_init() in C
section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 49
    dd isr %+ i
%assign i i+1
%endrep
