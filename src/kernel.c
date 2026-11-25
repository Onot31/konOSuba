#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/kernel-entrypoint.h"
#include "header/filesystem/ext2.h"
#include "header/process/process.h"
#include "header/process/scheduler.h"

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    initialize_filesystem_ext2();
    gdt_install_tss();
    set_tss_register();

    // Shell request
    uint32_t shell_parent = EXT2_ROOT_INODE;
    uint32_t bin_inode;
    uint8_t bin_type;
    if (find_directory_entry(EXT2_ROOT_INODE, ".bin", 4, &bin_inode, &bin_type)
        && bin_type == EXT2_FT_DIR)
        shell_parent = bin_inode;

    struct EXT2DriverRequest request = {
        .buf          = (uint8_t*) 0,
        .name         = "shell",
        .parent_inode = shell_parent,
        .buffer_size  = 0x100000,
        .name_len     = 5,
    };

    set_tss_kernel_current_stack();
    process_create_user_process(request);
    scheduler_init();
    scheduler_switch_to_next_process();
}