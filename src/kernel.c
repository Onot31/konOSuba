#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/cpu/portio.h"
#include "header/kernel-entrypoint.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"

/* Deklarasi fungsi dari Chapter 1 (sesuaikan dengan implementasi Anda) */
extern void pic_remap(void);
extern void initialize_idt(void);
extern void activate_keyboard_interrupt(void);
extern void framebuffer_clear(void);
extern void framebuffer_set_cursor(uint8_t r, uint8_t c);
extern void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    
    /* Initialize EXT2 File System */
    initialize_filesystem_ext2();
    
    /* === TEST: Write a file === */
    char test_data[] = "Hello, EXT2 File System!";
    struct EXT2DriverRequest write_req = {
        .buf          = test_data,
        .name         = "test.txt",
        .name_len     = 8,
        .parent_inode = EXT2_ROOT_INODE,
        .buffer_size  = sizeof(test_data),
        .is_directory = false,
    };
    int8_t write_result = write(write_req);
    
    /* Display write result */
    if (write_result == 0) {
        framebuffer_write(0, 0, 'W', 0x0, 0xF);
        framebuffer_write(0, 1, 'r', 0x0, 0xF);
        framebuffer_write(0, 2, 'i', 0x0, 0xF);
        framebuffer_write(0, 3, 't', 0x0, 0xF);
        framebuffer_write(0, 4, 'e', 0x0, 0xF);
        framebuffer_write(0, 5, ' ', 0x0, 0xF);
        framebuffer_write(0, 6, 'O', 0x0, 0xF);
        framebuffer_write(0, 7, 'K', 0x0, 0xF);
    }
    
    /* === TEST: Read the file back === */
    char read_buffer[512] = {0};
    struct EXT2DriverRequest read_req = {
        .buf          = read_buffer,
        .name         = "test.txt",
        .name_len     = 8,
        .parent_inode = EXT2_ROOT_INODE,
        .buffer_size  = sizeof(read_buffer),
        .is_directory = false,
    };
    int8_t read_result = read(read_req);
    
    /* Display read result */
    if (read_result == 0) {
        framebuffer_write(1, 0, 'R', 0x0, 0xF);
        framebuffer_write(1, 1, 'e', 0x0, 0xF);
        framebuffer_write(1, 2, 'a', 0x0, 0xF);
        framebuffer_write(1, 3, 'd', 0x0, 0xF);
        framebuffer_write(1, 4, ' ', 0x0, 0xF);
        framebuffer_write(1, 5, 'O', 0x0, 0xF);
        framebuffer_write(1, 6, 'K', 0x0, 0xF);
        
        /* Display first few chars of read data */
        for (int i = 0; i < 24 && read_buffer[i] != '\0'; i++) {
            framebuffer_write(2, i, read_buffer[i], 0x0, 0xF);
        }
    }
    
    while (true);
}