#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"
#include "header/cpu/idt.h"
#include "header/driver/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/kernel-entrypoint.h"

void kernel_setup(void) {
    // Chapter 0
    load_gdt(&_gdt_gdtr);
    
    // Chapter 1: Interrupt
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    
    // Chapter 1: Framebuffer
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    
    // Chapter 1: Keyboard Loop
    int row = 0, col = 0;
    keyboard_state_activate();
    
    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (c) {
            framebuffer_write(row, col, c, 0xF, 0); // White text, black bg
            if (col >= FRAMEBUFFER_WIDTH - 1) {
                ++row;
                col = 0;
            } else {
                ++col;
            }
            framebuffer_set_cursor(row, col);
        }
    }
}