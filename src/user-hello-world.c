/*
 * Aplikasi eksternal "Hello World!" (buku 4.3.4).
 * Dijalankan lewat "exec hello-world" atau langsung "hello-world".
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

int main(void) {
    static const char message[] = "Hello World!\n";
    syscall3(SYSCALL_PUTS, (uint32_t)message, (uint32_t)(sizeof(message) - 1), 0x0F);
    return 0;
}
