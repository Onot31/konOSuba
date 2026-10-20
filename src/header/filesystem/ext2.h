#ifndef _EXT2_H
#define _EXT2_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/driver/disk.h"

/* ===== Geometri disk ===== */
#define EXT2_DISK_SPACE         4194304u
#define EXT2_BLOCK_SIZE         BLOCK_SIZE                                 /* 512  */
#define EXT2_TOTAL_BLOCKS       (EXT2_DISK_SPACE / EXT2_BLOCK_SIZE)        /* 8192 */
#define EXT2_GROUPS_COUNT       8u
#define EXT2_BLOCKS_PER_GROUP   (EXT2_TOTAL_BLOCKS / EXT2_GROUPS_COUNT)   /* 1024 */
#define EXT2_INODE_SIZE         128u
#define EXT2_INODES_PER_BLOCK   (EXT2_BLOCK_SIZE / EXT2_INODE_SIZE)        /* 4    */
#define EXT2_INODE_TABLE_BLOCKS 16u
#define EXT2_INODES_PER_GROUP   (EXT2_INODES_PER_BLOCK * EXT2_INODE_TABLE_BLOCKS) /* 64  */
#define EXT2_INODES_COUNT       (EXT2_INODES_PER_GROUP * EXT2_GROUPS_COUNT)       /* 512 */
#define EXT2_PTRS_PER_BLOCK     (EXT2_BLOCK_SIZE / 4u)                     /* 128  */
#define EXT2_DIRECT_BLOCKS      12u

/* ===== Lokasi tetap ===== */
#define EXT2_BOOT_SECTOR_BLOCK  0u
#define EXT2_SUPERBLOCK_BLOCK   1u
#define EXT2_BGD_TABLE_BLOCK    2u

/* ===== Lokasi metadata tiap group ===== */
#define EXT2_GROUP_META_START(g)    ((g) == 0u ? 3u : (g) * EXT2_BLOCKS_PER_GROUP)
#define EXT2_GROUP_BLOCK_BITMAP(g)  (EXT2_GROUP_META_START(g))
#define EXT2_GROUP_INODE_BITMAP(g)  (EXT2_GROUP_META_START(g) + 1u)
#define EXT2_GROUP_INODE_TABLE(g)   (EXT2_GROUP_META_START(g) + 2u)
#define EXT2_GROUP_DATA_START(g)    (EXT2_GROUP_META_START(g) + 2u + EXT2_INODE_TABLE_BLOCKS)

/* ===== Konstanta lain ===== */
#define EXT2_SUPER_MAGIC        0xEF53
#define EXT2_ROOT_INODE         1u

#define EXT2_S_IFMT             0xF000
#define EXT2_S_IFREG            0x8000   /* regular file */
#define EXT2_S_IFDIR            0x4000   /* directory    */

#define EXT2_FT_UNKNOWN         0
#define EXT2_FT_REG_FILE        1
#define EXT2_FT_DIR             2

#define EXT2_DIRENT_HEADER      8u       /* sizeof(struct EXT2DirectoryEntry) */

/* ===== Struktur data ===== */

struct EXT2DriverRequest {
    void     *buf;
    char     *name;
    uint8_t   name_len;
    uint32_t  parent_inode;
    uint32_t  buffer_size;
    bool      is_directory;
} __attribute__((packed));

struct EXT2Superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_first_ino;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint16_t s_magic;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
} __attribute__((packed));

struct EXT2BlockGroupDescriptor {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed));

struct EXT2BlockGroupDescriptorTable {
    struct EXT2BlockGroupDescriptor table[EXT2_GROUPS_COUNT];
} __attribute__((packed));

struct EXT2Inode {
    uint16_t i_mode;          
    uint32_t i_size;          
    uint32_t i_atime;         
    uint32_t i_ctime;         
    uint32_t i_mtime;         
    uint32_t i_dtime;         
    uint16_t i_gid;           
    uint16_t i_links_count;   
    uint16_t i_pad;           
    uint32_t i_blocks;        
    uint32_t i_flags;         
    uint32_t i_osd1;          
    uint32_t i_block[15];     
    uint8_t  i_reserved[28];  
} __attribute__((packed));

struct EXT2DirectoryEntry {
    uint32_t inode;       
    uint16_t rec_len;     
    uint8_t  name_len;
    uint8_t  file_type;   
} __attribute__((packed));

_Static_assert(sizeof(struct EXT2Inode) == EXT2_INODE_SIZE, "EXT2Inode harus 128 byte");
_Static_assert(sizeof(struct EXT2BlockGroupDescriptor) == 32, "BGD harus 32 byte");
_Static_assert(sizeof(struct EXT2BlockGroupDescriptorTable) <= BLOCK_SIZE, "BGD table harus muat 1 block");
_Static_assert(sizeof(struct EXT2Superblock) <= BLOCK_SIZE, "Superblock harus muat 1 block");
_Static_assert(sizeof(struct EXT2DirectoryEntry) == EXT2_DIRENT_HEADER, "Header dirent harus 8 byte");

extern const uint8_t fs_signature[BLOCK_SIZE];

/* ===== Helper umum ===== */
uint32_t inode_to_bgd(uint32_t inode);
uint32_t inode_to_local(uint32_t inode);

char    *get_entry_name(struct EXT2DirectoryEntry *entry);
uint16_t get_entry_record_len(uint8_t name_len);

bool bitmap_test(const uint8_t *bitmap, uint32_t bit);
void bitmap_set(uint8_t *bitmap, uint32_t bit);
void bitmap_clear(uint8_t *bitmap, uint32_t bit);

bool read_inode(uint32_t inode, struct EXT2Inode *out);
bool write_inode(uint32_t inode, const struct EXT2Inode *in);

uint32_t get_inode_block(const struct EXT2Inode *inode, uint32_t index);

bool find_directory_entry(uint32_t dir_inode, const char *name, uint8_t name_len,
                          uint32_t *out_inode, uint8_t *out_type);

void commit_metadata(void);

/* ===== Allocator API ===== */
uint32_t allocate_block(uint32_t preferred_bgd);
bool     free_block(uint32_t block);
uint32_t allocate_inode(uint32_t preferred_bgd, bool is_dir);
bool     free_inode(uint32_t inode);

/* ===== Interface wajib ===== */
bool is_empty_storage(void);
void create_ext2(void);
void initialize_filesystem_ext2(void);

int8_t read(struct EXT2DriverRequest request);
int8_t read_directory(struct EXT2DriverRequest *prequest);
int8_t write(struct EXT2DriverRequest *request);
int8_t delete(struct EXT2DriverRequest request);

#endif