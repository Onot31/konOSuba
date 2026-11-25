/*
 * Program clock (buku 4.3.3): menampilkan jam:menit:detik di pojok kanan
 * bawah, memperbarui diri setiap detik. Berjalan sebagai proses user
 * terpisah sehingga bisa dihentikan dengan "kill" lalu dijalankan ulang.
 */

#include <stdint.h>
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

static void put_two(char *out, uint8_t value) {
    out[0] = (char)('0' + (value / 10u));
    out[1] = (char)('0' + (value % 10u));
}

int main(void) {
    static const char unavailable[] = "clock: syscall waktu belum tersedia\n";
    uint8_t last_second = 0xFF;

    for (;;) {
        struct TimeInfo now;

        if (syscall3(SYSCALL_GET_TIME, (uint32_t)&now, 0, 0) < 0) {
            syscall3(SYSCALL_PUTS, (uint32_t)unavailable,
                     (uint32_t)(sizeof(unavailable) - 1), 0x0C);
            return 1;
        }

        if (now.second != last_second) {
            char line[8];
            struct PutAtRequest request;

            last_second = now.second;
            put_two(line, now.hour);
            line[2] = ':';
            put_two(line + 3, now.minute);
            line[5] = ':';
            put_two(line + 6, now.second);

            request.row = CLOCK_ROW;
            request.col = CLOCK_COL;
            request.color = 0x0A;
            request._pad = 0;
            request.text = line;
            request.len = sizeof(line);
            syscall3(SYSCALL_PUTS_AT, (uint32_t)&request, 0, 0);
        }

        __asm__ volatile("pause");
    }
}
