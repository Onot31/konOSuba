/*
 * Aplikasi eksternal kedua (buku 4.3.4): pencacah yang mencetak satu baris
 * per detik. Proses terpisah dari shell, bisa dilihat lewat "ps" dan
 * dihentikan lewat "kill <PID>".
 */

#include <stdint.h>
#include "header/syscall.h"

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

static void put_text(const char *text, uint32_t len, uint8_t color) {
    syscall3(SYSCALL_PUTS, (uint32_t)text, len, color);
}

static void put_u32(uint32_t value) {
    char digits[10];
    uint32_t count = 0;

    if (value == 0) {
        syscall3(SYSCALL_PUTCHAR, '0', 0x0B, 0);
        return;
    }
    while (value > 0) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0) {
        count--;
        syscall3(SYSCALL_PUTCHAR, (uint32_t)(unsigned char)digits[count], 0x0B, 0);
    }
}

static void spin_delay(void) {
    for (volatile uint32_t i = 0; i < 3000000u; i++)
        __asm__ volatile("pause");
}

int main(void) {
    struct TimeInfo last;
    uint32_t count = 0;
    int time_ok;

    last.hour = 0xFF;
    last.minute = 0xFF;
    last.second = 0xFF;
    time_ok = syscall3(SYSCALL_GET_TIME, (uint32_t)&last, 0, 0) == 0;

    for (;;) {
        if (time_ok) {
            struct TimeInfo now;
            if (syscall3(SYSCALL_GET_TIME, (uint32_t)&now, 0, 0) == 0) {
                if (now.second == last.second) {
                    __asm__ volatile("pause");
                    continue;
                }
                last = now;
            } else {
                time_ok = 0;
            }
        }

        put_text("counter: ", 9, 0x0B);
        put_u32(count);
        put_text("\n", 1, 0x0B);
        count++;

        if (!time_ok) spin_delay();
    }
}
