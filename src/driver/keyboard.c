#include <stddef.h>
#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"

static struct KeyboardState keyboard_state = {
    .keyboard_input_on = false,
    .shift_pressed     = false,
    .caps_lock         = false,
    .extended_scancode = false,
    .head              = 0,
    .tail              = 0,
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

static const char keyboard_scancode_1_to_ascii_shift_map[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0
};

static void keyboard_push(char c) {
    uint32_t next = (keyboard_state.head + 1u) % KEYBOARD_BUFFER_SIZE;
    if (next == keyboard_state.tail)
        return;
    keyboard_state.keyboard_buffer[keyboard_state.head] = c;
    keyboard_state.head = next;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (scancode == 0xE0) {
        keyboard_state.extended_scancode = true;
        return;
    }

    if (scancode == 0xE1)
        return;

    bool released = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;

    if (keyboard_state.extended_scancode) {
        keyboard_state.extended_scancode = false;
        if (released) return;

        char key = 0;
        if (code == 0x48) key = (char)KEY_UP;
        else if (code == 0x50) key = (char)KEY_DOWN;
        else if (code == 0x4B) key = (char)KEY_LEFT;
        else if (code == 0x4D) key = (char)KEY_RIGHT;
        else if (code == 0x53) key = (char)KEY_DELETE;

        if (key != 0 && keyboard_state.keyboard_input_on)
            keyboard_push(key);
        return;
    }

    if (code == 0x2A || code == 0x36) {
        keyboard_state.shift_pressed = !released;
        return;
    }

    if (code == 0x3A) {
        if (!released) keyboard_state.caps_lock = !keyboard_state.caps_lock;
        return;
    }

    if (released)
        return;

    char ascii = keyboard_state.shift_pressed
        ? keyboard_scancode_1_to_ascii_shift_map[code]
        : keyboard_scancode_1_to_ascii_map[code];

    if (keyboard_state.caps_lock) {
        if (ascii >= 'a' && ascii <= 'z') ascii = (char)(ascii - 'a' + 'A');
        else if (ascii >= 'A' && ascii <= 'Z') ascii = (char)(ascii - 'A' + 'a');
    }

    if (ascii != 0 && keyboard_state.keyboard_input_on)
        keyboard_push(ascii);
}

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    if (buf == NULL)
        return;

    if (keyboard_state.head == keyboard_state.tail) {
        *buf = 0;
        return;
    }

    *buf = keyboard_state.keyboard_buffer[keyboard_state.tail];
    keyboard_state.tail = (keyboard_state.tail + 1u) % KEYBOARD_BUFFER_SIZE;
}
