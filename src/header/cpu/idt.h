#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

#define IDT_ENTRY_COUNT 256

/* Bit-bit tipe gate: 32-bit Interrupt Gate = 0b01110 (0xE) */
#define INTERRUPT_GATE_R_BIT_1  0b000   // bit 37-39
#define INTERRUPT_GATE_R_BIT_2  0b110   // bit 40-42
#define INTERRUPT_GATE_R_BIT_3  0b0     // bit 44 (S, harus 0 untuk system gate)

#define GDT_KERNEL_CODE_SEGMENT_SELECTOR 0x08

/**
 * IDTGate, entry IDT yang menunjuk ke interrupt handler.
 * Layout sesuai Intel x86 Vol 3A - Figure 6-2 (IDT Gate Descriptors).
 *
 * Byte hasil untuk handler 0x00101234, segment 0x08, DPL 0:
 *   34 12 08 00 00 8e 10 00
 */
struct IDTGate {
    // Bit 0 - 31
    uint16_t offset_low;
    uint16_t segment;

    // Bit 32 - 63
    uint8_t  _reserved  : 5;   // bit 32-36, selalu 0
    uint8_t  _r_bit_1   : 3;   // bit 37-39, INTERRUPT_GATE_R_BIT_1
    uint8_t  _r_bit_2   : 3;   // bit 40-42, INTERRUPT_GATE_R_BIT_2
    uint8_t  gate_32    : 1;   // bit 43, 1 = gate 32-bit
    uint8_t  _r_bit_3   : 1;   // bit 44, INTERRUPT_GATE_R_BIT_3 (S = 0)
    uint8_t  privilege  : 2;   // bit 45-46, DPL
    uint8_t  valid_bit  : 1;   // bit 47, Present
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

/**
 * Isi satu IDTGate dengan handler yang sesuai.
 *
 * @param int_vector       Nomor interrupt vector
 * @param handler_address  Alamat interrupt handler (ISR stub)
 * @param gdt_seg_selector Selector GDT, untuk kernel pakai GDT_KERNEL_CODE_SEGMENT_SELECTOR
 * @param privilege        Descriptor Privilege Level
 */
void set_interrupt_gate(uint8_t int_vector, void *handler_address,
                        uint16_t gdt_seg_selector, uint8_t privilege);

/**
 * Isi semua IDTGate dari isr_stub_table, load dengan lidt, lalu sti.
 */
void initialize_idt(void);

#endif