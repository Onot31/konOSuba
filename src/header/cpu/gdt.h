#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

/**
 * Segment Descriptor untuk GDT.
 * Urutan bitfield sangat krusial dan harus sesuai Intel Manual Vol 3A.
 */
struct SegmentDescriptor {
    // First 32-bit
    uint16_t segment_low;
    uint16_t base_low;
    
    // Next 16-bit (Bit 32 to 47)
    uint8_t base_mid;
    uint8_t type_bit   : 4;
    uint8_t non_system : 1;
    uint8_t privilege  : 2;
    uint8_t valid_bit  : 1;
    
    // Next 16-bit (Bit 48 to 63)
    uint8_t segment_high : 4;
    uint8_t available    : 1;
    uint8_t long_mode    : 1;
    uint8_t opr_32_bit   : 1;
    uint8_t granularity  : 1;
    uint8_t base_high;
} __attribute__((packed));

#define GDT_USER_CODE_SEGMENT_SELECTOR 0x18
#define GDT_USER_DATA_SEGMENT_SELECTOR 0x20
#define GDT_TSS_SELECTOR 0x28

struct GlobalDescriptorTable {
    struct SegmentDescriptor table[6];
} __attribute__((packed));

void gdt_install_tss(void);

struct GDTR {
    uint16_t size;
    uint32_t address;
} __attribute__((packed));

// Deklarasi variabel global dan fungsi assembly
extern struct GDTR _gdt_gdtr;
extern void load_gdt(struct GDTR *gdt_ptr);

#endif