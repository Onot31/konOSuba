#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"

struct TSSEntry _interrupt_tss_entry = {
    .ss0 = 0x10,
    // The upper 16 bits at offset 100 place the I/O bitmap beyond the TSS limit.
    .unused_register = {[22] = sizeof(struct TSSEntry) << 16},
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    __asm__ volatile("mov %%ebp, %0" : "=r"(stack_ptr));
    _interrupt_tss_entry.esp0 = stack_ptr + 8;
}

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8)
        out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    out(PIC1_DATA, PIC1_OFFSET); io_wait();
    out(PIC2_DATA, PIC2_OFFSET); io_wait();
    out(PIC1_DATA, 0b0100); io_wait();
    out(PIC2_DATA, 0b0010); io_wait();
    out(PIC1_DATA, ICW4_8086); io_wait();
    out(PIC2_DATA, ICW4_8086); io_wait();

    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            pic_ack(IRQ_KEYBOARD);
            break;
        default:
            if (frame.int_number >= PIC1_OFFSET && frame.int_number < PIC1_OFFSET + 8) {
                pic_ack((uint8_t)(frame.int_number - PIC1_OFFSET));
            } else if (frame.int_number >= PIC2_OFFSET && frame.int_number < PIC2_OFFSET + 8) {
                pic_ack((uint8_t)((frame.int_number - PIC2_OFFSET) + 8));
            }
            break;
    }
}