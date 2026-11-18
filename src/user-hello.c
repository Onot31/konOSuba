#include <stdint.h>

int main(void) {
    static const char message[] = "Hello from user mode!\n";
    uint32_t service = 6;
    uint32_t buffer = (uint32_t) message;
    uint32_t length = sizeof(message) - 1;
    uint32_t color = 0xF;
    __asm__ volatile("int $0x30"
        : "+a"(service), "+b"(buffer), "+c"(length), "+d"(color)
        :
        : "memory", "cc");
    return 0;
}
