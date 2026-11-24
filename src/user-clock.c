/*
 * Program clock (buku 4.3.3): menampilkan jam:menit:detik di pojok kanan
 * bawah, memperbarui diri setiap detik. Berjalan sebagai proses user
 * terpisah sehingga bisa dihentikan dengan "kill" lalu dijalankan ulang.
 */

#include <stdint.h>
#include <stdbool.h>
#include "header/syscall.h"

#define CLOCK_ROW 24
#define CLOCK_COL 68

static int32_t syscall3(uint32_t number, uint32_t a1, uint32_t a2, uint32_t a3) {
    int32_t result;
    __asm__ volatile(
        "int $0x30"
        : "=a"(result)
        : "a"(number), "b"(a1), "c"(a2), "d"(a3)
        : "memory", "cc"
    );
    return result;
}

static void put_text(const char *text, uint32_t len) {
    syscall3(SYSCALL_PUTS, (uint32_t)text, len, 0x0A);
}

static void put_char(char c) {
    syscall3(SYSCALL_PUTCHAR, (uint32_t)(unsigned char)c, 0x0A, 0);
}

static void put_two(uint8_t value) {
    put_char((char)('0' + (value / 10u)));
    put_char((char)('0' + (value % 10u)));
}

int main(void) {
    static const char unavailable[] = "clock: syscall waktu belum tersedia\n";
    uint8_t saved_row = 0;
    uint8_t saved_col = 0;
    uint8_t last_second = 0xFF;
    bool restore_cursor;

    restore_cursor = syscall3(SYSCALL_GET_CURSOR, (uint32_t)&saved_row,
                              (uint32_t)&saved_col, 0) == 0;

    for (;;) {
        struct TimeInfo now;

        if (syscall3(SYSCALL_GET_TIME, (uint32_t)&now, 0, 0) < 0) {
            put_text(unavailable, (uint32_t)(sizeof(unavailable) - 1));
            return 1;
        }

        if (now.second != last_second) {
            last_second = now.second;

            syscall3(SYSCALL_MOVE_CURSOR, CLOCK_ROW, CLOCK_COL, 0);
            put_two(now.hour);
            put_char(':');
            put_two(now.minute);
            put_char(':');
            put_two(now.second);
            if (restore_cursor)
                syscall3(SYSCALL_MOVE_CURSOR, saved_row, saved_col, 0);
        }

        __asm__ volatile("pause");
    }
}
