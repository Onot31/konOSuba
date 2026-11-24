global _start
extern main

section .text
_start:
    call main
    mov ebx, eax ; return code main()
    mov eax, 10 ; syscall exit (SYSCALL_EXIT)
    int 0x30