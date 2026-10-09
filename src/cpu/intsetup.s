%macro ISR_NOERRCODE 1
isr_stub_%1:
    push 0
    push %1
    jmp call_generic_handler
%endmacro

%macro ISR_ERRCODE 1
isr_stub_%1:
    push %1
    jmp call_generic_handler
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE 8
ISR_NOERRCODE 9
ISR_ERRCODE 10
ISR_ERRCODE 11
ISR_ERRCODE 12
ISR_ERRCODE 13
ISR_ERRCODE 14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_ERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE 30
ISR_NOERRCODE 31

%assign i 32
%rep 224
ISR_NOERRCODE i
%assign i i+1
%endrep

call_generic_handler:
    push ds
    push es
    push fs
    push gs
    push ebx
    push ecx
    push edx
    push eax
    push ebp
    push esi
    push edi

    push esp
    extern main_interrupt_handler
    call main_interrupt_handler
    pop esp

    pop edi
    pop esi
    pop ebp
    pop eax
    pop edx
    pop ecx
    pop ebx
    pop gs
    pop fs
    pop es
    pop ds

    add esp, 8
    iret

global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dd isr_stub_%+i
%assign i i+1
%endrep