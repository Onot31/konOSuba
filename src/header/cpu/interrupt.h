#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdint.h>

/* PIC Ports */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1
#define PIC_ACK         0x20
#define PIC_DISABLE_ALL_MASK 0xFF

/* ICW */
#define ICW1_INIT       0x10
#define ICW1_ICW4       0x01
#define ICW4_8086       0x01

/* PIC Offsets */
#define PIC1_OFFSET     0x20
#define PIC2_OFFSET     0x28

/* IRQ Numbers */
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1

/* Keyboard Data Port */
#define KEYBOARD_DATA_PORT 0x60

/**
 * CPURegister, store CPU registers values.
 */
struct CPURegister {
    struct { uint32_t edi; uint32_t esi; } __attribute__((packed)) index;
    struct { uint32_t ebp; uint32_t esp; } __attribute__((packed)) stack;
    struct { uint32_t ebx; uint32_t edx; uint32_t ecx; uint32_t eax; } __attribute__((packed)) general;
    struct { uint32_t gs; uint32_t fs; uint32_t es; uint32_t ds; } __attribute__((packed)) segment;
} __attribute__((packed));

/**
 * InterruptFrame, data pushed by CPU when interrupt occurs.
 */
struct InterruptFrame {
    struct CPURegister cpu;
    uint32_t int_number;
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} __attribute__((packed));

/* PIC Functions */
void io_wait(void);
void pic_ack(uint8_t irq);
void pic_remap(void);
void activate_keyboard_interrupt(void);

/* Main ISR */
void main_interrupt_handler(struct InterruptFrame frame);

#endif