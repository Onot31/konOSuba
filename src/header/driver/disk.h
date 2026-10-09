#ifndef _DISK_H
#define _DISK_H

#include <stdint.h>

#define BLOCK_SIZE      512
#define HALF_BLOCK_SIZE (BLOCK_SIZE / 2)

/* ATA Status Register bits */
#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_RDY  0x40
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

/**
 * BlockBuffer - Buffer untuk 1 block (512 bytes)
 */
struct BlockBuffer {
    uint8_t buf[BLOCK_SIZE];
} __attribute__((packed));

/**
 * read_blocks - Read blocks from disk (blocking)
 * @param ptr                   Pointer to destination buffer
 * @param logical_block_address LBA to read from
 * @param block_count           Number of blocks to read
 */
void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count);

/**
 * write_blocks - Write blocks to disk (blocking)
 * @param ptr                   Pointer to source buffer
 * @param logical_block_address LBA to write to
 * @param block_count           Number of blocks to write
 */
void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count);

#endif