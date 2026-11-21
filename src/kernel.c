#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/kernel-entrypoint.h"
#include "header/filesystem/ext2.h"

void kernel_setup(void) {
    // Chapter 0
    load_gdt(&_gdt_gdtr);
    
    // Chapter 1: Interrupt
    pic_remap();
    initialize_idt();
    initialize_filesystem_ext2();
    activate_keyboard_interrupt();
    
    // Chapter 1: Framebuffer
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    
    // disk image & driver test

    // Chapter 1: Keyboard Loop
    //int row = 0, col = 0;
    keyboard_state_activate();
    
    struct EXT2DriverRequest req = {
            .buf          = "haihai",
            .name         = "tes123",
            .name_len     = 2,
            .parent_inode = 1,
            .buffer_size  = 100,                  
            .is_directory = false
        };

    write(&req);
}