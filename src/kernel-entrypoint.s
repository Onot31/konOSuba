global loader
global load_gdt
extern kernel_setup

KERNEL_STACK_SIZE equ 4096           ; size of stack in bytes
MAGIC_NUMBER      equ 0x1BADB002     ; multiboot magic number
FLAGS             equ 0x0            ; multiboot flags
CHECKSUM          equ -MAGIC_NUMBER  ; checksum

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

; Prosedur untuk load GDT dan masuk ke Protected Mode
load_gdt:
    ; Ambil pointer GDTR dari argumen stack
    mov eax, [esp + 4]
    lgdt [eax]
    
    ; Set bit ke-0 (Protected Mode Enable) di CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Far jump ke Kernel Code Segment (0x08) untuk flush CS register
    jmp 0x08:.flush
.flush:
    ; Update data segment registers ke Kernel Data Segment (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret