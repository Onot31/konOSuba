#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/driver/framebuffer.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"

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

// syscall puts sementara
static void syscall_puts(const char *buf, uint32_t len, uint8_t color) {
    static uint8_t row = 0, col = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (buf[i] == '\n' || col >= FRAMEBUFFER_WIDTH) {
            row = (row + 1) % FRAMEBUFFER_HEIGHT;
            col = 0;
            if (buf[i] == '\n') continue;
        }
        framebuffer_write(row, col++, buf[i], color, 0);
    }
    framebuffer_set_cursor(row, col);
}

void syscall(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case SYSCALL_PUTS:
            syscall_puts((const char *) frame.cpu.general.ebx, frame.cpu.general.ecx, (uint8_t) frame.cpu.general.edx);
            break;
        case SYSCALL_EXIT: {
            // ebx belum dipakai
            struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
            if (current != NULL)
                process_destroy(current->metadata.pid);
            scheduler_switch_to_next_process();
        }
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_TIMER:
            pic_ack(IRQ_TIMER);
            // Context switch jika yang terinterupsi user process (ring 3)
            if ((frame.cs & 0x3) == 0x3) {
                struct Context ctx = {
                    .cpu                         = frame.cpu,
                    .eip                         = frame.eip,
                    .eflags                      = frame.eflags,
                    .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
                };
                scheduler_save_context_to_current_running_pcb(ctx);
                scheduler_switch_to_next_process();
            }
            break;
        case SYSCALL_INTERRUPT_VECTOR:
            syscall(frame);
            break;
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