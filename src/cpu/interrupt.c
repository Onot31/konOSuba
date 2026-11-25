#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/driver/framebuffer.h"
#include "header/driver/cmos.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"
#include "header/syscall.h"

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

void main_interrupt_handler(struct InterruptFrame *frame) {
    switch (frame->int_number) {
        case PIC1_OFFSET + IRQ_TIMER:
            pic_ack(IRQ_TIMER);
            // Context switch jika yang terinterupsi user process (ring 3)
            if ((frame->cs & 0x3) == 0x3) {
                struct Context ctx = {
                    .cpu                         = frame->cpu,
                    .eip                         = frame->eip,
                    .eflags                      = frame->eflags,
                    .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
                };
                scheduler_save_context_to_current_running_pcb(ctx);
                scheduler_switch_to_next_process();
            }
            break;
        case SYSCALL_INTERRUPT_VECTOR:
            frame->cpu.general.eax = syscall_dispatcher(frame);
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            pic_ack(IRQ_KEYBOARD);
            break;
        default:
            if (frame->int_number >= PIC1_OFFSET && frame->int_number < PIC1_OFFSET + 8) {
                pic_ack((uint8_t)(frame->int_number - PIC1_OFFSET));
            } else if (frame->int_number >= PIC2_OFFSET && frame->int_number < PIC2_OFFSET + 8) {
                pic_ack((uint8_t)((frame->int_number - PIC2_OFFSET) + 8));
            }
            break;
    }
}

static bool syscall_get_request(uint32_t addr, struct EXT2DriverRequest *out) {
    if (!paging_is_user_range((const void *)addr, sizeof(*out)))
        return false;

    memcpy(out, (const void *)addr, sizeof(*out));

    if (out->name_len > 0 && !paging_is_user_range(out->name, out->name_len))
        return false;
    if (out->buffer_size > 0 && out->buf != NULL
        && !paging_is_user_range(out->buf, out->buffer_size))
        return false;

    return true;
}

static uint32_t syscall_fs_result(int8_t retcode, uint32_t retcode_addr) {
    if (!paging_is_user_range((const void *)retcode_addr, 1))
        return (uint32_t)-1;
    *(int8_t *)retcode_addr = retcode;
    return (uint32_t)(int32_t)retcode;
}

static uint32_t syscall_ps(uint32_t buffer_addr, uint32_t capacity) {
    if (capacity > PROCESS_INFO_MAX)
        capacity = PROCESS_INFO_MAX;
    if (capacity == 0)
        return 0;
    if (!paging_is_user_range((const void *)buffer_addr,
                              capacity * sizeof(struct ProcessInfo)))
        return (uint32_t)-1;

    struct ProcessInfo *buffer = (struct ProcessInfo *)buffer_addr;
    uint32_t count = 0;

    for (uint32_t i = 0; i < PROCESS_COUNT_MAX && count < capacity; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_INACTIVE)
            continue;

        struct ProcessInfo info;
        memset(&info, 0, sizeof(info));
        info.pid   = _process_list[i].metadata.pid;
        info.state = (uint32_t)_process_list[i].metadata.state;
        memcpy(info.name, _process_list[i].metadata.name, PROCESS_INFO_NAME_LEN);
        info.name[PROCESS_INFO_NAME_LEN - 1] = '\0';
        buffer[count++] = info;
    }
    return count;
}

uint32_t syscall_dispatcher(struct InterruptFrame *frame) {
    uint32_t syscall_num = frame->cpu.general.eax;
    uint32_t arg1 = frame->cpu.general.ebx;
    uint32_t arg2 = frame->cpu.general.ecx;
    uint32_t arg3 = frame->cpu.general.edx;

    switch (syscall_num) {
        case SYSCALL_FS_READ:
        case SYSCALL_FS_READ_DIRECTORY:
        case SYSCALL_FS_WRITE:
        case SYSCALL_FS_DELETE:
            {
                struct EXT2DriverRequest request;
                if (!syscall_get_request(arg1, &request))
                    return (uint32_t)-1;

                int8_t retcode;
                if (syscall_num == SYSCALL_FS_READ) {
                    if (request.buffer_size > 0 && request.buf == NULL)
                        return (uint32_t)-1;
                    retcode = read(request);
                } else if (syscall_num == SYSCALL_FS_READ_DIRECTORY) {
                    if (request.buf == NULL)
                        return (uint32_t)-1;
                    retcode = read_directory(&request);
                } else if (syscall_num == SYSCALL_FS_WRITE) {
                    if (request.buffer_size > 0 && request.buf == NULL)
                        return (uint32_t)-1;
                    retcode = write(&request);
                } else {
                    retcode = delete(request);
                }
                return syscall_fs_result(retcode, arg2);
            }

        case SYSCALL_GETCHAR:
            {
                if (!paging_is_user_range((const void *)arg1, 1))
                    return (uint32_t)-1;

                char c = 0;
                get_keyboard_buffer(&c);
                *(char *)arg1 = c;
                return (uint32_t)(uint8_t)c;
            }

        case SYSCALL_PUTCHAR:
            {
                uint8_t color = (uint8_t)arg2;
                framebuffer_putchar((char)arg1, color & 0x0F, (color >> 4) & 0x0F);
                return 0;
            }

        case SYSCALL_PUTS:
            {
                if (arg2 > 0 && !paging_is_user_range((const void *)arg1, arg2))
                    return (uint32_t)-1;

                uint8_t color = (uint8_t)arg3;
                framebuffer_puts((const char *)arg1, arg2,
                                 color & 0x0F, (color >> 4) & 0x0F);
                return 0;
            }

        case SYSCALL_KEYBOARD_ACTIVATE:
            keyboard_state_activate();
            return 0;

        case SYSCALL_EXIT:
            {
                struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
                if (current != NULL)
                    process_destroy(current->metadata.pid);
                scheduler_switch_to_next_process();
                return 0;
            }

        case SYSCALL_EXEC:
            {
                if (arg3 == 0 || arg3 > 255
                    || !paging_is_user_range((const void *)arg2, arg3))
                    return (uint32_t)-1;

                char name[256];
                memcpy(name, (const void *)arg2, arg3);
                name[arg3] = '\0';

                struct EXT2DriverRequest request = {
                    .buf          = NULL,
                    .name         = name,
                    .name_len     = (uint8_t)arg3,
                    .parent_inode = arg1,
                    .buffer_size  = 0x100000,
                    .is_directory = false,
                };
                int32_t create_retcode = process_create_user_process(request);
                return create_retcode == PROCESS_CREATE_SUCCESS
                    ? 0
                    : (uint32_t)(-create_retcode);
            }

        case SYSCALL_PS:
            return syscall_ps(arg1, arg2);

        case SYSCALL_KILL:
            {
                struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
                if (current != NULL && current->metadata.pid == arg1)
                    return (uint32_t)-1;
                return process_destroy(arg1) ? 0 : (uint32_t)-1;
            }

        case SYSCALL_GET_TIME:
            {
                if (!paging_is_user_range((const void *)arg1, sizeof(struct TimeInfo)))
                    return (uint32_t)-1;
                return cmos_read_time((struct TimeInfo *)arg1) ? 0 : (uint32_t)-1;
            }

        case SYSCALL_MOVE_CURSOR:
            framebuffer_set_cursor((uint8_t)arg1, (uint8_t)arg2);
            return 0;

        case SYSCALL_GET_CURSOR:
            {
                if (!paging_is_user_range((const void *)arg1, 1)
                    || !paging_is_user_range((const void *)arg2, 1))
                    return (uint32_t)-1;
                framebuffer_get_cursor((uint8_t *)arg1, (uint8_t *)arg2);
                return 0;
            }

        default:
            return (uint32_t)-1;
    }
}