global process_context_switch

; struct CPURegister: index (edi, esi), stack (ebp, esp), general (ebx, edx, ecx, eax), segment (gs, fs, es, ds)
CTX_EDI    equ 0
CTX_ESI    equ 4
CTX_EBP    equ 8
CTX_ESP    equ 12
CTX_EBX    equ 16
CTX_EDX    equ 20
CTX_ECX    equ 24
CTX_EAX    equ 28
CTX_GS     equ 32
CTX_FS     equ 36
CTX_ES     equ 40
CTX_DS     equ 44
CTX_EIP    equ 48
CTX_EFLAGS equ 52

USER_CODE_SELECTOR equ 0x18 | 0x3 ; GDT user code segment, RPL 3

section .text
; void process_context_switch(struct Context ctx)
process_context_switch:
    ; simpan base address argument ctx
    lea  ecx, [esp + 4]

    ; stack untuk iret inter-privilege: ss, esp, eflags, cs, eip
    push dword [ecx + CTX_DS]
    push dword [ecx + CTX_ESP]
    push dword [ecx + CTX_EFLAGS]
    push dword USER_CODE_SELECTOR
    push dword [ecx + CTX_EIP]

    ; load segment register
    mov  eax, [ecx + CTX_GS]
    mov  gs, ax
    mov  eax, [ecx + CTX_FS]
    mov  fs, ax
    mov  eax, [ecx + CTX_ES]
    mov  es, ax
    mov  eax, [ecx + CTX_DS]
    mov  ds, ax

    ; load general purpose & index register
    mov  edi, [ecx + CTX_EDI]
    mov  esi, [ecx + CTX_ESI]
    mov  ebp, [ecx + CTX_EBP]
    mov  ebx, [ecx + CTX_EBX]
    mov  edx, [ecx + CTX_EDX]
    mov  eax, [ecx + CTX_EAX]
    mov  ecx, [ecx + CTX_ECX]
    iret