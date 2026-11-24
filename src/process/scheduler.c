// TODO (Bagian C): stub minimal Round Robin agar process & context switch bisa dites, silakan diganti/dilengkapi.
#include "header/process/scheduler.h"
#include "header/cpu/portio.h"

#define PIT_MAX_FREQUENCY   1193182
#define PIT_TIMER_FREQUENCY 1000
#define PIT_TIMER_COUNTER   (PIT_MAX_FREQUENCY / PIT_TIMER_FREQUENCY)

#define PIT_COMMAND_REGISTER_PIO          0x43
#define PIT_COMMAND_VALUE_BINARY_MODE     0b0
#define PIT_COMMAND_VALUE_OPR_SQUARE_WAVE (0b011 << 1)
#define PIT_COMMAND_VALUE_ACC_LOHIBYTE    (0b11 << 4)
#define PIT_COMMAND_VALUE_CHANNEL         (0b00 << 6)
#define PIT_COMMAND_VALUE (PIT_COMMAND_VALUE_BINARY_MODE | PIT_COMMAND_VALUE_OPR_SQUARE_WAVE | PIT_COMMAND_VALUE_ACC_LOHIBYTE | PIT_COMMAND_VALUE_CHANNEL)

#define PIT_CHANNEL_0_DATA_PIO 0x40

static void activate_timer_interrupt(void) {
    __asm__ volatile("cli");
    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}

void scheduler_init(void) {
    activate_timer_interrupt();
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL)
        current->context = ctx;
}

void scheduler_switch_to_next_process(void) {
    // Round Robin
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    int32_t start_index = PROCESS_COUNT_MAX - 1;
    if (current != NULL) {
        current->metadata.state = PROCESS_STATE_READY;
        start_index = current - _process_list;
    }

    for (int32_t i = 1; i <= PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *next = &_process_list[(start_index + i) % PROCESS_COUNT_MAX];
        if (next->metadata.state == PROCESS_STATE_READY) {
            next->metadata.state = PROCESS_STATE_RUNNING;
            paging_use_page_directory(next->context.page_directory_virtual_addr);
            process_context_switch(next->context);
        }
    }

    while (true)
        __asm__ volatile("sti; hlt");
}