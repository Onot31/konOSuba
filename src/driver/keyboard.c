#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"

static struct KeyboardState keyboard_state = {
    .keyboard_input_on = false,
    .keyboard_buffer   = 0
};

static const char keyboard_scancode_1_to_ascii_map[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0
};

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (keyboard_state.keyboard_input_on) {
        if (!(scancode & 0x80)) { // Ignore Break codes
            char ascii = keyboard_scancode_1_to_ascii_map[scancode];
            if (ascii != 0) {
                keyboard_state.keyboard_buffer = ascii;
            }
        }
    }
}

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0;
}