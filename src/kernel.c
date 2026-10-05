#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"

void kernel_setup(void) {
    // Load GDT dan masuk ke Protected Mode
    load_gdt(&_gdt_gdtr);
    
    // Infinite loop untuk menandakan kernel berhasil booting tanpa Triple Fault
    while (true);
}