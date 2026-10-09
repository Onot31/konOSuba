#include <stdint.h>
#include <stdbool.h>
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

/* ===== Static Variables ===== */
static struct EXT2SuperBlock ext2_superblock;
static struct EXT2BlockGroupDescriptor ext2_bg_desc;

/* ===== Helper Functions ===== */

static void write_boot_sector(void) {
    struct BlockBuffer buf = {0};
    memcpy(buf.buf, EXT2_FS_SIGNATURE, EXT2_FS_SIG_LEN);
    write_blocks(&buf, EXT2_BOOT_SECTOR_BLOCK, 1);
}

static void write_superblock(void) {
    ext2_superblock.s_inodes_count      = EXT2_INODES_PER_GROUP;
    ext2_superblock.s_blocks_count      = EXT2_TOTAL_BLOCKS;
    ext2_superblock.s_free_blocks_count = EXT2_TOTAL_BLOCKS - EXT2_DATA_BLOCK_START;
    ext2_superblock.s_free_inodes_count = EXT2_INODES_PER_GROUP - 1; /* Root used */
    ext2_superblock.s_first_data_block  = EXT2_DATA_BLOCK_START;
    ext2_superblock.s_block_size        = EXT2_BLOCK_SIZE;
    ext2_superblock.s_blocks_per_group  = EXT2_BLOCKS_PER_GROUP;
    ext2_superblock.s_inodes_per_group  = EXT2_INODES_PER_GROUP;
    ext2_superblock.s_inode_size        = EXT2_INODE_SIZE;
    memcpy(ext2_superblock.s_volume_name, "konOSuba", 8);
    
    struct BlockBuffer buf = {0};
    memcpy(buf.buf, &ext2_superblock, sizeof(struct EXT2SuperBlock));
    write_blocks(&buf, EXT2_SUPERBLOCK_BLOCK, 1);
}

static void write_block_group_descriptor(void) {
    ext2_bg_desc.bg_block_bitmap      = EXT2_BLOCK_BITMAP_BLOCK;
    ext2_bg_desc.bg_inode_bitmap      = EXT2_INODE_BITMAP_BLOCK;
    ext2_bg_desc.bg_inode_table       = EXT2_INODE_TABLE_BLOCK;
    ext2_bg_desc.bg_free_blocks_count = EXT2_TOTAL_BLOCKS - EXT2_DATA_BLOCK_START;
    ext2_bg_desc.bg_free_inodes_count = EXT2_INODES_PER_GROUP - 1;
    ext2_bg_desc.bg_used_dirs_count   = 1;
    ext2_bg_desc.bg_pad               = 0;
    
    struct BlockBuffer buf = {0};
    memcpy(buf.buf, &ext2_bg_desc, sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&buf, EXT2_BLOCK_GROUP_DESC_BLOCK, 1);
}

static void initialize_block_bitmap(void) {
    struct BlockBuffer buf = {0};
    /* Mark blocks 0-6 as used (boot, superblock, bg_desc, bitmaps, inode table) */
    for (uint32_t i = 0; i < EXT2_DATA_BLOCK_START; i++) {
        buf.buf[i / 8] |= (1 << (i % 8));
    }
    write_blocks(&buf, EXT2_BLOCK_BITMAP_BLOCK, 1);
}

static void initialize_inode_bitmap(void) {
    struct BlockBuffer buf = {0};
    /* Mark inode 0 and 1 as reserved, inode 2 (root) as used */
    buf.buf[0] = 0x07; /* bits 0, 1, 2 set */
    write_blocks(&buf, EXT2_INODE_BITMAP_BLOCK, 1);
}

static void initialize_root_inode(void) {
    struct BlockBuffer inode_table_buf[EXT2_INODE_TABLE_BLOCKS] = {{0}};
    
    struct EXT2Inode root_inode = {0};
    root_inode.i_mode       = EXT2_FT_DIR;
    root_inode.i_size       = EXT2_BLOCK_SIZE;
    root_inode.i_links_count = 1;
    root_inode.i_blocks     = 1;
    root_inode.i_block[0]   = EXT2_DATA_BLOCK_START; /* Root dir data at first data block */
    
    /* Write root inode at index 2 (0-indexed in table) */
    uint32_t inode_offset = (EXT2_ROOT_INODE - 1) * EXT2_INODE_SIZE;
    uint32_t block_idx = inode_offset / EXT2_BLOCK_SIZE;
    uint32_t offset_in_block = inode_offset % EXT2_BLOCK_SIZE;
    memcpy(&inode_table_buf[block_idx].buf[offset_in_block], &root_inode, sizeof(struct EXT2Inode));
    
    for (uint32_t i = 0; i < EXT2_INODE_TABLE_BLOCKS; i++) {
        write_blocks(&inode_table_buf[i], EXT2_INODE_TABLE_BLOCK + i, 1);
    }
    
    /* Initialize root directory data block with . and .. entries */
    struct BlockBuffer dir_buf = {0};
    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) dir_buf.buf;
    
    /* Entry 0: "." (self) */
    entry->inode     = EXT2_ROOT_INODE;
    entry->rec_len   = 12;
    entry->name_len  = 1;
    entry->file_type = EXT2_FT_DIR;
    entry->name[0]   = '.';
    
    /* Entry 1: ".." (parent = self for root) */
    entry = (struct EXT2DirectoryEntry*) (dir_buf.buf + 12);
    entry->inode     = EXT2_ROOT_INODE;
    entry->rec_len   = EXT2_BLOCK_SIZE - 12;
    entry->name_len  = 2;
    entry->file_type = EXT2_FT_DIR;
    entry->name[0]   = '.';
    entry->name[1]   = '.';
    
    write_blocks(&dir_buf, EXT2_DATA_BLOCK_START, 1);
}

static void read_inode(uint32_t inode_num, struct EXT2Inode *inode) {
    struct BlockBuffer buf[EXT2_INODE_TABLE_BLOCKS];
    for (uint32_t i = 0; i < EXT2_INODE_TABLE_BLOCKS; i++) {
        read_blocks(&buf[i], EXT2_INODE_TABLE_BLOCK + i, 1);
    }
    
    uint32_t inode_offset = (inode_num - 1) * EXT2_INODE_SIZE;
    uint32_t block_idx = inode_offset / EXT2_BLOCK_SIZE;
    uint32_t offset_in_block = inode_offset % EXT2_BLOCK_SIZE;
    memcpy(inode, &buf[block_idx].buf[offset_in_block], sizeof(struct EXT2Inode));
}

static void write_inode(uint32_t inode_num, const struct EXT2Inode *inode) {
    struct BlockBuffer buf;
    uint32_t inode_offset = (inode_num - 1) * EXT2_INODE_SIZE;
    uint32_t block_idx = inode_offset / EXT2_BLOCK_SIZE;
    uint32_t offset_in_block = inode_offset % EXT2_BLOCK_SIZE;
    
    read_blocks(&buf, EXT2_INODE_TABLE_BLOCK + block_idx, 1);
    memcpy(&buf.buf[offset_in_block], inode, sizeof(struct EXT2Inode));
    write_blocks(&buf, EXT2_INODE_TABLE_BLOCK + block_idx, 1);
}

static bool get_block_bitmap(uint32_t block_num) {
    struct BlockBuffer buf;
    read_blocks(&buf, EXT2_BLOCK_BITMAP_BLOCK, 1);
    return (buf.buf[block_num / 8] >> (block_num % 8)) & 1;
}

static void set_block_bitmap(uint32_t block_num, bool used) {
    struct BlockBuffer buf;
    read_blocks(&buf, EXT2_BLOCK_BITMAP_BLOCK, 1);
    if (used)
        buf.buf[block_num / 8] |= (1 << (block_num % 8));
    else
        buf.buf[block_num / 8] &= ~(1 << (block_num % 8));
    write_blocks(&buf, EXT2_BLOCK_BITMAP_BLOCK, 1);
}

static bool get_inode_bitmap(uint32_t inode_num) {
    struct BlockBuffer buf;
    read_blocks(&buf, EXT2_INODE_BITMAP_BLOCK, 1);
    return (buf.buf[inode_num / 8] >> (inode_num % 8)) & 1;
}

static void set_inode_bitmap(uint32_t inode_num, bool used) {
    struct BlockBuffer buf;
    read_blocks(&buf, EXT2_INODE_BITMAP_BLOCK, 1);
    if (used)
        buf.buf[inode_num / 8] |= (1 << (inode_num % 8));
    else
        buf.buf[inode_num / 8] &= ~(1 << (inode_num % 8));
    write_blocks(&buf, EXT2_INODE_BITMAP_BLOCK, 1);
}

static uint32_t allocate_block(void) {
    for (uint32_t i = EXT2_DATA_BLOCK_START; i < EXT2_TOTAL_BLOCKS; i++) {
        if (!get_block_bitmap(i)) {
            set_block_bitmap(i, true);
            ext2_superblock.s_free_blocks_count--;
            return i;
        }
    }
    return 0; /* No free block */
}

static uint32_t allocate_inode(void) {
    for (uint32_t i = 1; i <= EXT2_INODES_PER_GROUP; i++) {
        if (!get_inode_bitmap(i)) {
            set_inode_bitmap(i, true);
            ext2_superblock.s_free_inodes_count--;
            return i;
        }
    }
    return 0; /* No free inode */
}

static void free_block(uint32_t block_num) {
    set_block_bitmap(block_num, false);
    ext2_superblock.s_free_blocks_count++;
}

static void free_inode(uint32_t inode_num) {
    set_inode_bitmap(inode_num, false);
    ext2_superblock.s_free_inodes_count++;
}

static int8_t find_entry_in_directory(uint32_t dir_inode_num, const char *name, 
                                       uint8_t name_len, struct EXT2DirectoryEntry *result) {
    struct EXT2Inode dir_inode;
    read_inode(dir_inode_num, &dir_inode);
    
    if (dir_inode.i_mode != EXT2_FT_DIR)
        return -1;
    
    struct BlockBuffer dir_data;
    read_blocks(&dir_data, dir_inode.i_block[0], 1);
    
    uint32_t offset = 0;
    while (offset < dir_inode.i_size) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (dir_data.buf + offset);
        if (entry->inode != 0 && entry->name_len == name_len) {
            if (memcmp(entry->name, name, name_len) == 0) {
                if (result)
                    memcpy(result, entry, sizeof(struct EXT2DirectoryEntry));
                return 0; /* Found */
            }
        }
        if (entry->rec_len == 0)
            break;
        offset += entry->rec_len;
    }
    return -1; /* Not found */
}

static void write_directory_entry(uint32_t dir_inode_num, uint32_t new_inode, 
                                   const char *name, uint8_t name_len, uint8_t file_type) {
    struct EXT2Inode dir_inode;
    read_inode(dir_inode_num, &dir_inode);
    
    struct BlockBuffer dir_data;
    read_blocks(&dir_data, dir_inode.i_block[0], 1);
    
    /* Find last entry */
    uint32_t offset = 0;
    struct EXT2DirectoryEntry *last_entry = NULL;
    while (offset < dir_inode.i_size) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (dir_data.buf + offset);
        if (entry->inode != 0) {
            last_entry = entry;
        }
        if (entry->rec_len == 0)
            break;
        offset += entry->rec_len;
    }
    
    /* Calculate new entry size (aligned to 4 bytes) */
    uint16_t entry_size = 8 + 1 + name_len; /* inode(4) + rec_len(2) + name_len(2) + type(1) + name */
    entry_size = (entry_size + 3) & ~3;     /* Align to 4 bytes */
    
    /* Split last entry's rec_len */
    if (last_entry) {
        uint16_t old_rec_len = last_entry->rec_len;
        last_entry->rec_len = entry_size;
        
        struct EXT2DirectoryEntry *new_entry = 
            (struct EXT2DirectoryEntry*) ((uint8_t*)last_entry + entry_size);
        new_entry->inode     = new_inode;
        new_entry->rec_len   = old_rec_len - entry_size;
        new_entry->name_len  = name_len;
        new_entry->file_type = file_type;
        memcpy(new_entry->name, name, name_len);
    }
    
    write_blocks(&dir_data, dir_inode.i_block[0], 1);
}

static void remove_directory_entry(uint32_t dir_inode_num, const char *name, uint8_t name_len) {
    struct EXT2Inode dir_inode;
    read_inode(dir_inode_num, &dir_inode);
    
    struct BlockBuffer dir_data;
    read_blocks(&dir_data, dir_inode.i_block[0], 1);
    
    uint32_t offset = 0;
    struct EXT2DirectoryEntry *prev_entry = NULL;
    
    while (offset < dir_inode.i_size) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (dir_data.buf + offset);
        if (entry->inode != 0 && entry->name_len == name_len) {
            if (memcmp(entry->name, name, name_len) == 0) {
                /* Found - merge with previous entry */
                if (prev_entry) {
                    prev_entry->rec_len += entry->rec_len;
                } else {
                    entry->inode = 0;
                }
                break;
            }
        }
        prev_entry = entry;
        if (entry->rec_len == 0)
            break;
        offset += entry->rec_len;
    }
    
    write_blocks(&dir_data, dir_inode.i_block[0], 1);
}

static bool is_directory_empty(uint32_t dir_inode_num) {
    struct EXT2Inode dir_inode;
    read_inode(dir_inode_num, &dir_inode);
    
    struct BlockBuffer dir_data;
    read_blocks(&dir_data, dir_inode.i_block[0], 1);
    
    uint32_t offset = 0;
    uint32_t entry_count = 0;
    
    while (offset < dir_inode.i_size) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (dir_data.buf + offset);
        if (entry->inode != 0) {
            entry_count++;
        }
        if (entry->rec_len == 0)
            break;
        offset += entry->rec_len;
    }
    
    /* Empty directory has exactly 2 entries: "." and ".." */
    return entry_count <= 2;
}

/* ===== Public Interface Functions ===== */

void create_ext2(void) {
    write_boot_sector();
    write_superblock();
    write_block_group_descriptor();
    initialize_block_bitmap();
    initialize_inode_bitmap();
    initialize_root_inode();
}

bool is_empty_storage(void) {
    struct BlockBuffer buf;
    read_blocks(&buf, EXT2_BOOT_SECTOR_BLOCK, 1);
    return memcmp(buf.buf, EXT2_FS_SIGNATURE, EXT2_FS_SIG_LEN) != 0;
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        /* Load superblock from block 1 */
        struct BlockBuffer sb_buf;
        read_blocks(&sb_buf, EXT2_SUPERBLOCK_BLOCK, 1);
        memcpy(&ext2_superblock, sb_buf.buf, sizeof(struct EXT2SuperBlock));
        
        /* Load block group descriptor from block 2 */
        struct BlockBuffer bg_buf;
        read_blocks(&bg_buf, EXT2_BLOCK_GROUP_DESC_BLOCK, 1);
        memcpy(&ext2_bg_desc, bg_buf.buf, sizeof(struct EXT2BlockGroupDescriptor));
    }
}

int8_t read(struct EXT2DriverRequest request) {
    /* Validate parent inode */
    struct EXT2Inode parent_inode;
    read_inode(request.parent_inode, &parent_inode);
    if (parent_inode.i_mode != EXT2_FT_DIR)
        return 4; /* Parent folder invalid */
    
    /* Find entry in parent directory */
    struct EXT2DirectoryEntry entry;
    if (find_entry_in_directory(request.parent_inode, request.name, request.name_len, &entry) != 0)
        return 3; /* File not found */
    
    /* Check if it's a file (not directory) */
    if (entry.file_type != EXT2_FT_REG_FILE)
        return 1; /* Not a file */
    
    /* Read inode of the file */
    struct EXT2Inode file_inode;
    read_inode(entry.inode, &file_inode);
    
    /* Check buffer size */
    if (request.buffer_size < file_inode.i_size)
        return 2; /* Buffer not enough */
    
    /* Read file data */
    uint32_t blocks_to_read = (file_inode.i_size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    uint8_t *dest = (uint8_t*) request.buf;
    
    for (uint32_t i = 0; i < blocks_to_read && i < 12; i++) {
        if (file_inode.i_block[i] == 0)
            break;
        struct BlockBuffer buf;
        read_blocks(&buf, file_inode.i_block[i], 1);
        uint32_t bytes_to_copy = EXT2_BLOCK_SIZE;
        if (file_inode.i_size - (i * EXT2_BLOCK_SIZE) < bytes_to_copy)
            bytes_to_copy = file_inode.i_size - (i * EXT2_BLOCK_SIZE);
        memcpy(dest + (i * EXT2_BLOCK_SIZE), buf.buf, bytes_to_copy);
    }
    
    return 0; /* Success */
}

int8_t read_directory(struct EXT2DriverRequest request) {
    /* Validate parent inode */
    struct EXT2Inode parent_inode;
    read_inode(request.parent_inode, &parent_inode);
    if (parent_inode.i_mode != EXT2_FT_DIR)
        return 3; /* Parent folder invalid */
    
    /* Find entry in parent directory */
    struct EXT2DirectoryEntry entry;
    if (find_entry_in_directory(request.parent_inode, request.name, request.name_len, &entry) != 0)
        return 2; /* Folder not found */
    
    /* Check if it's a directory */
    if (entry.file_type != EXT2_FT_DIR)
        return 1; /* Not a folder */
    
    /* Read inode of the directory */
    struct EXT2Inode dir_inode;
    read_inode(entry.inode, &dir_inode);
    
    /* Read directory data into buffer */
    struct BlockBuffer dir_data;
    read_blocks(&dir_data, dir_inode.i_block[0], 1);
    
    uint32_t copy_size = dir_inode.i_size;
    if (copy_size > request.buffer_size)
        copy_size = request.buffer_size;
    
    memcpy(request.buf, dir_data.buf, copy_size);
    
    return 0; /* Success */
}

int8_t write(struct EXT2DriverRequest request) {
    /* Validate parent inode */
    struct EXT2Inode parent_inode;
    read_inode(request.parent_inode, &parent_inode);
    if (parent_inode.i_mode != EXT2_FT_DIR)
        return 2; /* Parent folder invalid */
    
    /* Check if name already exists */
    struct EXT2DirectoryEntry existing_entry;
    if (find_entry_in_directory(request.parent_inode, request.name, request.name_len, &existing_entry) == 0)
        return 1; /* Entry with name already exists */
    
    /* Allocate new inode */
    uint32_t new_inode_num = allocate_inode();
    if (new_inode_num == 0)
        return -1; /* No free inode */
    
    /* Initialize new inode */
    struct EXT2Inode new_inode = {0};
    
    if (request.is_directory) {
        new_inode.i_mode = EXT2_FT_DIR;
        new_inode.i_size = EXT2_BLOCK_SIZE;
        new_inode.i_blocks = 1;
        
        /* Allocate data block for directory */
        uint32_t dir_block = allocate_block();
        new_inode.i_block[0] = dir_block;
        
        /* Write . and .. entries */
        struct BlockBuffer dir_buf = {0};
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) dir_buf.buf;
        
        /* Entry 0: "." */
        entry->inode     = new_inode_num;
        entry->rec_len   = 12;
        entry->name_len  = 1;
        entry->file_type = EXT2_FT_DIR;
        entry->name[0]   = '.';
        
        /* Entry 1: ".." */
        entry = (struct EXT2DirectoryEntry*) (dir_buf.buf + 12);
        entry->inode     = request.parent_inode;
        entry->rec_len   = EXT2_BLOCK_SIZE - 12;
        entry->name_len  = 2;
        entry->file_type = EXT2_FT_DIR;
        entry->name[0]   = '.';
        entry->name[1]   = '.';
        
        write_blocks(&dir_buf, dir_block, 1);
        
        /* Update directory count */
        ext2_bg_desc.bg_used_dirs_count++;
    } else {
        new_inode.i_mode = EXT2_FT_REG_FILE;
        new_inode.i_size = request.buffer_size;
        
        /* Allocate blocks for file data */
        uint32_t blocks_needed = (request.buffer_size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
        new_inode.i_blocks = blocks_needed;
        
        const uint8_t *src = (const uint8_t*) request.buf;
        for (uint32_t i = 0; i < blocks_needed && i < 12; i++) {
            uint32_t block = allocate_block();
            if (block == 0)
                return -1;
            new_inode.i_block[i] = block;
            
            struct BlockBuffer buf = {0};
            uint32_t bytes_to_copy = EXT2_BLOCK_SIZE;
            if (request.buffer_size - (i * EXT2_BLOCK_SIZE) < bytes_to_copy)
                bytes_to_copy = request.buffer_size - (i * EXT2_BLOCK_SIZE);
            memcpy(buf.buf, src + (i * EXT2_BLOCK_SIZE), bytes_to_copy);
            write_blocks(&buf, block, 1);
        }
    }
    
    new_inode.i_links_count = 1;
    
    /* Write new inode to inode table */
    write_inode(new_inode_num, &new_inode);
    
    /* Add directory entry to parent */
    uint8_t file_type = request.is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    write_directory_entry(request.parent_inode, new_inode_num, request.name, request.name_len, file_type);
    
    /* Commit metadata */
    write_superblock();
    
    struct BlockBuffer bg_buf;
    memcpy(bg_buf.buf, &ext2_bg_desc, sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&bg_buf, EXT2_BLOCK_GROUP_DESC_BLOCK, 1);
    
    return 0; /* Success */
}

int8_t delete(struct EXT2DriverRequest request) {
    /* Validate parent inode */
    struct EXT2Inode parent_inode;
    read_inode(request.parent_inode, &parent_inode);
    if (parent_inode.i_mode != EXT2_FT_DIR)
        return 3; /* Parent folder invalid */
    
    /* Find entry in parent directory */
    struct EXT2DirectoryEntry entry;
    if (find_entry_in_directory(request.parent_inode, request.name, request.name_len, &entry) != 0)
        return 1; /* Entry not found */
    
    /* Cannot delete root */
    if (entry.inode == EXT2_ROOT_INODE)
        return 1;
    
    /* If directory, check if empty */
    if (entry.file_type == EXT2_FT_DIR) {
        if (!is_directory_empty(entry.inode))
            return 2; /* Folder not empty */
        ext2_bg_desc.bg_used_dirs_count--;
    }
    
    /* Free blocks used by the inode */
    struct EXT2Inode target_inode;
    read_inode(entry.inode, &target_inode);
    
    for (int i = 0; i < 12; i++) {
        if (target_inode.i_block[i] != 0) {
            free_block(target_inode.i_block[i]);
        }
    }
    
    /* Free inode */
    free_inode(entry.inode);
    
    /* Remove directory entry from parent */
    remove_directory_entry(request.parent_inode, request.name, request.name_len);
    
    /* Commit metadata */
    write_superblock();
    
    struct BlockBuffer bg_buf;
    memcpy(bg_buf.buf, &ext2_bg_desc, sizeof(struct EXT2BlockGroupDescriptor));
    write_blocks(&bg_buf, EXT2_BLOCK_GROUP_DESC_BLOCK, 1);
    
    return 0; /* Success */
}