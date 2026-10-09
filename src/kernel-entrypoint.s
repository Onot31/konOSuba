global loader
global load_gdt
extern kernel_setup

KERNEL_STACK_SIZE equ 2097152          ; 2 MiB (WAJIB untuk Chapter 2+)
MAGIC_NUMBER      equ 0x1BADB002
FLAGS             equ 0x0
CHECKSUM          equ -MAGIC_NUMBER

section .bss
align 4
kernel_stack:
    resb KERNEL_STACK_SIZE

section .multiboot
align 4
    dd MAGIC_NUMBER
    dd FLAGS
    dd CHECKSUM

section .text
align 4

loader:
    mov esp, kernel_stack + KERNEL_STACK_SIZE
    call kernel_setup
.loop:
    jmp .loop

load_gdt:
    mov eax, [esp + 4]
    lgdt [eax]
    
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    jmp 0x08:.flush
.flush:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret