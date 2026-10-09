#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

struct KeyboardState {
    bool keyboard_input_on;
    char keyboard_buffer;
} __attribute__((packed));

void keyboard_isr(void);
void keyboard_state_activate(void);
void keyboard_state_deactivate(void);
void get_keyboard_buffer(char *buf);

#endif