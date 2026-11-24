#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/memory/paging.h"

static void pde_set_frame(struct PageDirectoryEntry *entry, uint32_t frame) {
    uint32_t raw;
    memcpy(&raw, entry, sizeof(raw));
    raw = (raw & 0xFFFu) | (frame << 12);
    memcpy(entry, &raw, sizeof(raw));
}

__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;

__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;

struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1] = true,
        [KERNEL_RESERVED_PAGE_FRAME_COUNT ... PAGE_FRAME_MAX_COUNT - 1] = false
    },
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index) {
    //this helper function is given from kit but you can always modified it if you think theres a bug (spoiler alert) or you think theres a better approach

    if (page_directory_index == RECURSIVE_PAGE_DIRECTORY_INDEX) return NULL; /*|| page_dir != paging_get_current_page_directory_addr() <- implemented in Ch4*/

    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];

    if (dir_entry->flag.present_bit) {
        return (struct PageTable *)(RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);
    }
    for (uint32_t page_frame_number = KERNEL_RESERVED_PAGE_FRAME_COUNT;
         page_frame_number < PAGE_FRAME_MAX_COUNT;
         page_frame_number++) {
        if (!page_manager_state.page_frame_map[page_frame_number]) {
            page_manager_state.page_frame_map[page_frame_number] = true;
            page_manager_state.free_page_frame_count--;
            dir_entry->flag = (struct PageDirectoryEntryFlag) {
                .present_bit = true,
                .write_bit = true,
                .user_bit = true,
            };
            pde_set_frame(dir_entry, page_frame_number);
            struct PageTable *page_table = (struct PageTable *)(RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS + page_directory_index * PAGE_FRAME_SIZE);
            flush_single_tlb(page_table);
            memset(page_table, 0, PAGE_FRAME_SIZE);
            return page_table;
        }
    }
    return NULL;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageTableEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t)virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index = ((uint32_t)virtual_addr >> 12) & 0x3FF;
    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return;
    page_table->table[page_table_index].flag = flag;
    page_table->table[page_table_index].page_address = ((uint32_t) physical_addr >> 12);
    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    asm volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr): "memory");
}

bool paging_allocate_check(uint32_t amount) {
    uint32_t required_frames = amount / PAGE_FRAME_SIZE + (amount % PAGE_FRAME_SIZE > 0);

    if (required_frames <= page_manager_state.free_page_frame_count) {
        return true;
    }
    return false;
}


bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    /**
     * TODO: Find free physical frame and map virtual frame into it
     * - Find free physical frame in page_manager_state.page_frame_map[] using any strategies
     * - Mark page_manager_state.page_frame_map[]
     * - Update page directory with user flags:
     *     > present bit    true
     *     > write bit      true
     *     > user bit       true
     *     > pagesize 4 mb  true
     */ 

    // check if allocation possible
    if (!paging_allocate_check(PAGE_FRAME_SIZE)) {
        return false;
    }

    // Find free physical frame in page_manager_state.page_frame_map[]
    uint32_t map_index = KERNEL_RESERVED_PAGE_FRAME_COUNT;
    while (page_manager_state.page_frame_map[map_index] != 0 && map_index < PAGE_FRAME_MAX_COUNT) {
        map_index++;
    }
    if (map_index >= PAGE_FRAME_MAX_COUNT) {
        return false;
    }

    // mark position
    page_manager_state.page_frame_map[map_index] = true;
    page_manager_state.free_page_frame_count--;

    // update page directory
    struct PageTableEntryFlag flag = (struct PageTableEntryFlag) {
        .present_bit = true,
        .write_bit = true,
        .user_bit = true
    };
    void *physical_addr = (void *) (map_index * PAGE_FRAME_SIZE);

    update_page_directory_entry(page_dir, physical_addr, virtual_addr, flag);
    
    return true;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    /* 
     * TODO: Deallocate a physical frame from respective virtual address
     * - Use the page_dir.table values to check mapped physical frame
     * - Remove the entry by setting it into 0
     */
    uint32_t page_directory_index = ((uint32_t)virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index = ((uint32_t)virtual_addr >> 12) & 0x3FF;
    struct PageTable *page_table = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL) return false;

    // remove entry
    struct PageTableEntry *entry = &page_table->table[page_table_index];
    if (!entry->flag.present_bit) return false;

    // find map index
    uint32_t physical_address = page_table->table[page_table_index].page_address << 12;
    if (physical_address < 0x400000) return false;

    uint32_t map_index = physical_address / PAGE_FRAME_SIZE;
    if (map_index >= PAGE_FRAME_MAX_COUNT) return false;

    // remove frame 
    if (page_manager_state.page_frame_map[map_index] == false) return false;

    *entry = (struct PageTableEntry) {0};
    page_manager_state.page_frame_map[map_index] = false;
    page_manager_state.free_page_frame_count++;
    
    flush_single_tlb(virtual_addr);

    return true;
}


/* --- Process-related Memory Management --- */
// TODO (Bagian A): stub minimal agar process bisa dites, silakan diganti/dilengkapi.
__attribute__((aligned(0x1000))) static struct PageDirectory page_directory_list[PAGING_DIRECTORY_TABLE_MAX_COUNT] = {0};

static struct {
    bool page_directory_used[PAGING_DIRECTORY_TABLE_MAX_COUNT];
} page_directory_manager = {
    .page_directory_used = {false},
};

struct PageDirectory* paging_create_new_page_directory(void) {
    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (page_directory_manager.page_directory_used[i]) continue;
        page_directory_manager.page_directory_used[i] = true;

        struct PageDirectory *page_dir = &page_directory_list[i];
        memset(page_dir, 0, sizeof(struct PageDirectory));

        // Kernel higher half (0xC0000000 - 0xC03FFFFF): pakai page table kernel yang sama
        page_dir->table[0x300] = _paging_kernel_page_directory.table[0x300];

        // Recursive mapping agar paging_get_page_table() bekerja saat page directory ini aktif
        struct PageDirectoryEntry *recursive_entry = &page_dir->table[RECURSIVE_PAGE_DIRECTORY_INDEX];
        recursive_entry->flag.present_bit = true;
        recursive_entry->flag.write_bit   = true;
        pde_set_frame(recursive_entry, ((uint32_t) page_dir - 0xC0000000) >> 12);
        return page_dir;
    }
    return NULL;
}

bool paging_free_page_directory(struct PageDirectory *page_dir) {
    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (&page_directory_list[i] == page_dir) {
            page_directory_manager.page_directory_used[i] = false;
            memset(page_dir, 0, sizeof(struct PageDirectory));
            return true;
        }
    }
    return false;
}

struct PageDirectory* paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_page_directory_phys_addr): /* <Empty> */);
    uint32_t virtual_addr_page_dir = current_page_directory_phys_addr + 0xC0000000;
    return (struct PageDirectory*) virtual_addr_page_dir;
}

void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr) {
    uint32_t physical_addr_page_dir = (uint32_t) page_dir_virtual_addr;
    // Additional layer of check & mistake safety net
    if ((uint32_t) page_dir_virtual_addr > 0xC0000000)
        physical_addr_page_dir -= 0xC0000000;
    __asm__  volatile("mov %0, %%cr3" : /* <Empty> */ : "r"(physical_addr_page_dir): "memory");
}