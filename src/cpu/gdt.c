#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"

/**
 * global_descriptor_table, predefined GDT.
 * Entry: Null, Kernel Code, Kernel Data, User Code, User Data, TSS
 */
struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        // 0: Null Descriptor
        {
            .segment_low = 0, .base_low = 0, .base_mid = 0,
            .type_bit = 0, .non_system = 0, .privilege = 0, .valid_bit = 0,
            .segment_high = 0, .available = 0, .long_mode = 0, .opr_32_bit = 0,
            .granularity = 0, .base_high = 0
        },
        // 1: Kernel Code Segment (Base 0, Limit 0xFFFFF, Type 0xA, DPL 0)
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0xA,       // 1010: Code, Readable
            .non_system = 1,       // 1: Code or Data Segment
            .privilege = 0,        // 0: Ring 0 (Kernel)
            .valid_bit = 1,        // 1: Present
            .segment_high = 0xF,   // Limit 19:16
            .available = 0, .long_mode = 0,
            .opr_32_bit = 1,       // 1: 32-bit protected mode
            .granularity = 1,      // 1: 4KB granularity
            .base_high = 0
        },
        // 2: Kernel Data Segment (Base 0, Limit 0xFFFFF, Type 0x2, DPL 0)
        {
            .segment_low = 0xFFFF, .base_low = 0, .base_mid = 0,
            .type_bit = 0x2,       // 0010: Data, Writable
            .non_system = 1,       // 1: Code or Data Segment
            .privilege = 0,        // 0: Ring 0 (Kernel)
            .valid_bit = 1,        // 1: Present
            .segment_high = 0xF,   // Limit 19:16
            .available = 0, .long_mode = 0,
            .opr_32_bit = 1,       // 1: 32-bit protected mode
            .granularity = 1,      // 1: 4KB granularity
            .base_high = 0
        },
        {
            .segment_low = 0xFFFF,
            .segment_high = 0xF,
            .type_bit = 0xA,
            .non_system = 1,
            .privilege = 3,
            .valid_bit = 1,
            .opr_32_bit = 1,
            .granularity = 1,
        },
        {
            .segment_low = 0xFFFF,
            .segment_high = 0xF,
            .type_bit = 0x2,
            .non_system = 1,
            .privilege = 3,
            .valid_bit = 1,
            .opr_32_bit = 1,
            .granularity = 1,
        },
        {
            .segment_low = sizeof(struct TSSEntry) - 1,
            .segment_high = ((sizeof(struct TSSEntry) - 1) >> 16) & 0xF,
            .type_bit = 0x9,
            .valid_bit = 1,
        }
    }
};

void gdt_install_tss(void) {
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    global_descriptor_table.table[5].base_high = (base & (0xFFu << 24)) >> 24;
    global_descriptor_table.table[5].base_mid = (base & (0xFFu << 16)) >> 16;
    global_descriptor_table.table[5].base_low = base & 0xFFFF;
}

/**
 * _gdt_gdtr, predefined system GDTR.
 * Mengarah ke global_descriptor_table. Size adalah sizeof - 1.
 */
struct GDTR _gdt_gdtr = {
    .size = sizeof(global_descriptor_table) - 1,
    .address = (uint32_t) &global_descriptor_table
};