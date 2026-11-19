#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};

static struct {
    uint32_t active_process_count;
    uint32_t next_pid;
} process_manager_state = {
    .active_process_count = 0,
    .next_pid             = 0,
};

static uint32_t ceil_div(uint32_t numerator, uint32_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_INACTIVE)
            return i;
    }
    return -1;
}

static uint32_t process_generate_new_pid(void) {
    return process_manager_state.next_pid++;
}

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (_process_list[i].metadata.state == PROCESS_STATE_RUNNING)
            return &_process_list[i];
    }
    return NULL;
}

static bool process_allocate_page(struct ProcessControlBlock *pcb, void *virtual_addr) {
    if (!paging_allocate_user_page_frame(paging_get_current_page_directory_addr(), virtual_addr))
        return false;
    memset(virtual_addr, 0, PAGE_FRAME_SIZE);
    pcb->memory.virtual_addr_used[pcb->memory.page_frame_used_count++] = virtual_addr;
    return true;
}

static void process_release_memory(struct ProcessControlBlock *pcb) {
    struct PageDirectory *page_dir = pcb->context.page_directory_virtual_addr;
    struct PageDirectory *prev_dir = paging_get_current_page_directory_addr();

    paging_use_page_directory(page_dir);
    for (uint32_t i = 0; i < pcb->memory.page_frame_used_count; i++)
        paging_free_user_page_frame(page_dir, pcb->memory.virtual_addr_used[i]);
    pcb->memory.page_frame_used_count = 0;
    paging_use_page_directory(prev_dir == page_dir ? &_paging_kernel_page_directory : prev_dir);
    paging_free_page_directory(page_dir);
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX)
        return PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
    uint32_t program_start = (uint32_t) request.buf;
    if (program_start >= KERNEL_VIRTUAL_ADDRESS_BASE || request.buffer_size > PROCESS_USER_STACK_PAGE_ADDR - program_start)
        return PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;

    uint32_t page_start              = program_start & ~(PAGE_FRAME_SIZE - 1);
    uint32_t program_end             = program_start + request.buffer_size;
    uint32_t page_frame_count_needed = ceil_div(program_end - page_start, PAGE_FRAME_SIZE) + 1;
    if (page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX || !paging_allocate_check((page_frame_count_needed + 3) * PAGE_FRAME_SIZE))
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;

    char name[256];
    memcpy(name, request.name, request.name_len);
    request.name = name;

    // virtual address space baru
    int32_t p_index = process_list_get_inactive_index();
    struct ProcessControlBlock *new_pcb = &_process_list[p_index];
    struct PageDirectory *page_dir = paging_create_new_page_directory();
    if (page_dir == NULL)
        return PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
    new_pcb->context.page_directory_virtual_addr = page_dir;

    // alokasi memory & load executable
    struct PageDirectory *prev_dir = paging_get_current_page_directory_addr();
    paging_use_page_directory(page_dir);

    bool alloc_success = true;
    for (uint32_t addr = page_start; addr < program_end && alloc_success; addr += PAGE_FRAME_SIZE)
        alloc_success = process_allocate_page(new_pcb, (void *) addr);
    if (alloc_success)
        alloc_success = process_allocate_page(new_pcb, (void *) PROCESS_USER_STACK_PAGE_ADDR);
    int8_t read_retcode = alloc_success ? read(request) : -1;

    paging_use_page_directory(prev_dir);

    if (!alloc_success || read_retcode != 0) {
        process_release_memory(new_pcb);
        memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
        return alloc_success ? PROCESS_CREATE_FAIL_FS_READ_FAILURE : PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
    }

    uint32_t user_data_selector = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    new_pcb->context.cpu.segment.ds = user_data_selector;
    new_pcb->context.cpu.segment.es = user_data_selector;
    new_pcb->context.cpu.segment.fs = user_data_selector;
    new_pcb->context.cpu.segment.gs = user_data_selector;
    new_pcb->context.cpu.stack.esp  = PROCESS_USER_STACK_TOP;
    new_pcb->context.eip            = program_start;
    new_pcb->context.eflags         = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;

    uint32_t name_len = request.name_len < PROCESS_NAME_LENGTH_MAX - 1 ? request.name_len : PROCESS_NAME_LENGTH_MAX - 1;
    memcpy(new_pcb->metadata.name, name, name_len);
    new_pcb->metadata.pid   = process_generate_new_pid();
    new_pcb->metadata.state = PROCESS_STATE_READY;
    process_manager_state.active_process_count++;

    return PROCESS_CREATE_SUCCESS;
}

bool process_destroy(uint32_t pid) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *pcb = &_process_list[i];
        if (pcb->metadata.state != PROCESS_STATE_INACTIVE && pcb->metadata.pid == pid) {
            process_release_memory(pcb);
            memset(pcb, 0, sizeof(struct ProcessControlBlock));
            process_manager_state.active_process_count--;
            return true;
        }
    }
    return false;
}