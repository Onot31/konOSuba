#include "header/driver/framebuffer.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"

static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

static void framebuffer_update_cursor(void) {
    uint16_t position = (uint16_t)cursor_row * FRAMEBUFFER_WIDTH + cursor_col;
    out(0x3D4, 0x0F);
    out(0x3D5, (uint8_t)(position & 0xFF));
    out(0x3D4, 0x0E);
    out(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

static void framebuffer_clear_row(uint8_t row) {
    for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++)
        framebuffer_write(row, col, ' ', 0x0F, 0);
}

static void framebuffer_scroll(void) {
    memmove(
        FRAMEBUFFER_MEMORY_OFFSET,
        FRAMEBUFFER_MEMORY_OFFSET + FRAMEBUFFER_WIDTH * 2,
        (FRAMEBUFFER_TEXT_HEIGHT - 1) * FRAMEBUFFER_WIDTH * 2
    );
    framebuffer_clear_row(FRAMEBUFFER_TEXT_HEIGHT - 1);
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    if (row >= FRAMEBUFFER_HEIGHT || col >= FRAMEBUFFER_WIDTH)
        return;

    uint32_t offset = ((uint32_t)row * FRAMEBUFFER_WIDTH + col) * 2;
    FRAMEBUFFER_MEMORY_OFFSET[offset]     = (uint8_t)c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (uint8_t)((bg << 4) | (fg & 0x0F));
}

void framebuffer_putchar(char c, uint8_t fg, uint8_t bg) {
    switch (c) {
        case '\n':
            cursor_col = 0;
            cursor_row++;
            break;
        case '\r':
            cursor_col = 0;
            break;
        case '\b':
            if (cursor_col > 0) cursor_col--;
            else if (cursor_row > 0) {
                cursor_row--;
                cursor_col = FRAMEBUFFER_WIDTH - 1;
            }
            break;
        default:
            framebuffer_write(cursor_row, cursor_col, c, fg, bg);
            cursor_col++;
            if (cursor_col >= FRAMEBUFFER_WIDTH) {
                cursor_col = 0;
                cursor_row++;
            }
            break;
    }

    while (cursor_row >= FRAMEBUFFER_TEXT_HEIGHT) {
        framebuffer_scroll();
        cursor_row--;
    }
    framebuffer_update_cursor();
}

void framebuffer_puts(const char *s, uint32_t len, uint8_t fg, uint8_t bg) {
    if (s == NULL)
        return;

    for (uint32_t i = 0; i < len; i++)
        framebuffer_putchar(s[i], fg, bg);
}

void framebuffer_puts_at(uint8_t row, uint8_t col, const char *s, uint32_t len, uint8_t fg, uint8_t bg) {
    if (s == NULL || row >= FRAMEBUFFER_HEIGHT)
        return;

    for (uint32_t i = 0; i < len && (uint32_t)col + i < FRAMEBUFFER_WIDTH; i++)
        framebuffer_write(row, (uint8_t)(col + i), s[i], fg, bg);
}

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    cursor_row = r < FRAMEBUFFER_HEIGHT ? r : FRAMEBUFFER_HEIGHT - 1;
    cursor_col = c < FRAMEBUFFER_WIDTH ? c : FRAMEBUFFER_WIDTH - 1;
    framebuffer_update_cursor();
}

void framebuffer_get_cursor(uint8_t *r, uint8_t *c) {
    if (r != NULL) *r = cursor_row;
    if (c != NULL) *c = cursor_col;
}

void framebuffer_clear(void) {
    memset(FRAMEBUFFER_MEMORY_OFFSET, 0, FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 2);
    cursor_row = 0;
    cursor_col = 0;
    framebuffer_update_cursor();
}
