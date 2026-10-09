#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

#define IDT_ENTRY_COUNT 256
#define INTERRUPT_GATE_R_BIT_1  0b1110
#define INTERRUPT_GATE_R_BIT_2  0b0000
#define INTERRUPT_GATE_R_BIT_3  0b0000
#define GDT_KERNEL_CODE_SEGMENT_SELECTOR 0x08

struct IDTGate {
    uint16_t offset_low;
    uint16_t segment;
    uint8_t  _r_bit_1 : 4;
    uint8_t  _r_bit_2 : 4;
    uint8_t  _r_bit_3 : 4;
    uint8_t  privilege : 2;
    uint8_t  valid_bit : 1;
    uint8_t  gate_32   : 1;
    uint16_t offset_high;
} __attribute__((packed));

struct InterruptDescriptorTable {
    struct IDTGate table[IDT_ENTRY_COUNT];
} __attribute__((packed));

struct IDTR {
    uint16_t size;
    uint32_t address;
} __attribute__((packed));

extern struct InterruptDescriptorTable interrupt_descriptor_table;
extern struct IDTR _idt_idtr;

void initialize_idt(void);
void set_interrupt_gate(uint8_t int_vector, void *handler_address,
                        uint16_t gdt_seg_selector, uint8_t privilege);

#endif