#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"

void activate_keyboard_interrupt(void)
{
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void main_interrupt_handler(struct InterruptFrame frame)
{
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_KEYBOARD:
            /*
             * IRQ1 keyboard.
             * Sesuai buku, keyboard_isr() wajib memanggil pic_ack(IRQ_KEYBOARD).
             */
            keyboard_isr();
            break;

        default:
            /*
             * ACK untuk interrupt PIC lainnya.
             */
            if (frame.int_number >= PIC1_OFFSET &&
                frame.int_number < PIC1_OFFSET + 8) {
                pic_ack((uint8_t)(frame.int_number - PIC1_OFFSET));
            } else if (frame.int_number >= PIC2_OFFSET &&
                       frame.int_number < PIC2_OFFSET + 8) {
                pic_ack((uint8_t)((frame.int_number - PIC2_OFFSET) + 8));
            }
            break;
    }
}