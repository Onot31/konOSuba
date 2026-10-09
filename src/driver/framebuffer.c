#include "header/driver/framebuffer.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint32_t offset = (row * FRAMEBUFFER_WIDTH + col) * 2;
    FRAMEBUFFER_MEMORY_OFFSET[offset]     = (uint8_t)c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (bg << 4) | (fg & 0x0F);
}

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t position = r * FRAMEBUFFER_WIDTH + c;
    out(0x3D4, 0x0F);
    out(0x3D5, (uint8_t)(position & 0xFF));
    out(0x3D4, 0x0E);
    out(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

void framebuffer_clear(void) {
    memset(FRAMEBUFFER_MEMORY_OFFSET, 0, FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 2);
}