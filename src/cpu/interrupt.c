#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/filesystem/ext2.h"
#include "header/driver/framebuffer.h"

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

uint32_t syscall_dispatcher(struct InterruptFrame *frame) {
    uint32_t syscall_num = frame->cpu.general.eax;

    switch (syscall_num) {
        case 0: // Syscall 0: Read
            return read(*(struct EXT2DriverRequest*)frame->cpu.general.ebx);

        case 1: // Syscall 1: Read Directory
            return (uint32_t)-1;

        case 2: // Syscall 2: Write
            return write((struct EXT2DriverRequest*)frame->cpu.general.ebx);

        case 3: // Syscall 3: Delete
            return (uint32_t)-1;

        case 4: // Syscall 4: Keyboard Getchar
            {
                char c = 0;
                get_keyboard_buffer(&c);
                return (uint32_t)c;
            }

        case 5: // Syscall 5: Putchar (Tulis karakter ke layar)
            framebuffer_write(frame->cpu.general.ecx, frame->cpu.general.edx, (char)frame->cpu.general.ebx, frame->cpu.index.esi, 0);
            return 0;

        case 6: // Syscall 6: Puts (Tulis string ke layar)
            {
                char* str = (char*)frame->cpu.general.ebx;
                uint32_t current_col = frame->cpu.general.edx;
                uint32_t current_row = frame->cpu.general.ecx;
                
                while (*str != '\0') {
                    framebuffer_write(current_row, current_col, *str, frame->cpu.index.esi, 0);
                    current_col++;
                    if (current_col >= 80) {
                        current_col = 0;
                        current_row++;
                    }
                    str++;
                }
            }
            return 0;

        case 7: // Syscall 7: Keyboard Activate
            keyboard_state_activate();
            return 0;

        default: // Syscall tidak valid
            return (uint32_t)-1;
    }
}