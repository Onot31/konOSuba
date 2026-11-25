#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_BUFFER_SIZE 256

#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_DELETE  0x84

struct KeyboardState {
    bool     keyboard_input_on;
    bool     shift_pressed;
    bool     caps_lock;
    bool     extended_scancode;
    char     keyboard_buffer[KEYBOARD_BUFFER_SIZE];
    uint32_t head;
    uint32_t tail;
} __attribute__((packed));

void keyboard_isr(void);
void keyboard_state_activate(void);
void keyboard_state_deactivate(void);
void get_keyboard_buffer(char *buf);

#endif
