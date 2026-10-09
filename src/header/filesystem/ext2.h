#ifndef _EXT2_H
#define _EXT2_H

#include <stdint.h>
#include <stdbool.h>

/* ===== EXT2 IF2130 Edition Constants ===== */
#define EXT2_BLOCK_SIZE         512
#define EXT2_BLOCKS_PER_GROUP   8192
#define EXT2_INODES_PER_GROUP   8
#define EXT2_INODE_SIZE         128
#define EXT2_INODES_PER_BLOCK   (EXT2_BLOCK_SIZE / EXT2_INODE_SIZE)  /* 4 */

/* Layout: Block indices */
#define EXT2_BOOT_SECTOR_BLOCK      0
#define EXT2_SUPERBLOCK_BLOCK       1
#define EXT2_BLOCK_GROUP_DESC_BLOCK 2
#define EXT2_BLOCK_BITMAP_BLOCK     3
#define EXT2_INODE_BITMAP_BLOCK     4
#define EXT2_INODE_TABLE_BLOCK      5
#define EXT2_INODE_TABLE_BLOCKS     2
#define EXT2_DATA_BLOCK_START       (EXT2_INODE_TABLE_BLOCK + EXT2_INODE_TABLE_BLOCKS)  /* 7 */
#define EXT2_TOTAL_BLOCKS           8192  /* 4 MiB / 512 bytes */

/* Inode types */
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2

/* Reserved inodes */
#define EXT2_ROOT_INODE     2

/* Max filename length */
#define EXT2_MAX_FILENAME   255

/* FS Signature */
#define EXT2_FS_SIGNATURE   "IF2130-OS"
#define EXT2_FS_SIG_LEN     9

/* ===== Data Structures ===== */

/**
 * EXT2DirectoryEntry - Entry dalam directory
 */
struct EXT2DirectoryEntry {
    uint32_t inode;         /* Inode number (0 = unused) */
    uint16_t rec_len;       /* Record length (total size of this entry) */
    uint16_t name_len;      /* Name length */
    uint8_t  file_type;     /* File type (EXT2_FT_*) */
    char     name[EXT2_MAX_FILENAME]; /* File name (NOT null-terminated in disk) */
} __attribute__((packed));

/**
 * EXT2Inode - Inode structure
 */
struct EXT2Inode {
    uint16_t i_mode;        /* File type and permissions */
    uint16_t i_uid;         /* User ID (unused in IF2130) */
    uint32_t i_size;        /* Size in bytes */
    uint32_t i_atime;       /* Access time (unused) */
    uint32_t i_ctime;       /* Creation time (unused) */
    uint32_t i_mtime;       /* Modification time (unused) */
    uint32_t i_dtime;       /* Deletion time (unused) */
    uint16_t i_gid;         /* Group ID (unused) */
    uint16_t i_links_count; /* Links count */
    uint32_t i_blocks;      /* Number of 512-byte blocks reserved */
    uint32_t i_flags;       /* Flags (unused) */
    uint32_t i_reserved;    /* Reserved */
    uint32_t i_block[15];   /* Block pointers (12 direct + 3 indirect) */
} __attribute__((packed));

/**
 * EXT2SuperBlock - Superblock structure
 */
struct EXT2SuperBlock {
    uint32_t s_inodes_count;        /* Total inodes */
    uint32_t s_blocks_count;        /* Total blocks */
    uint32_t s_free_blocks_count;   /* Free blocks */
    uint32_t s_free_inodes_count;   /* Free inodes */
    uint32_t s_first_data_block;    /* First data block */
    uint32_t s_block_size;          /* Block size */
    uint32_t s_blocks_per_group;    /* Blocks per group */
    uint32_t s_inodes_per_group;    /* Inodes per group */
    uint32_t s_inode_size;          /* Inode size in bytes */
    char     s_volume_name[16];     /* Volume name */
} __attribute__((packed));

/**
 * EXT2BlockGroupDescriptor - Block group descriptor
 */
struct EXT2BlockGroupDescriptor {
    uint32_t bg_block_bitmap;       /* Block bitmap location */
    uint32_t bg_inode_bitmap;       /* Inode bitmap location */
    uint32_t bg_inode_table;        /* Inode table start */
    uint16_t bg_free_blocks_count;  /* Free blocks in group */
    uint16_t bg_free_inodes_count;  /* Free inodes in group */
    uint16_t bg_used_dirs_count;    /* Used directories count */
    uint16_t bg_pad;                /* Padding */
} __attribute__((packed));

/**
 * EXT2DriverRequest - Request parameter untuk CRUD interface
 */
struct EXT2DriverRequest {
    void     *buf;              /* Buffer for data */
    char     *name;             /* File/folder name */
    uint8_t  name_len;          /* Name length */
    uint32_t parent_inode;      /* Parent inode number */
    uint32_t buffer_size;       /* Buffer size */
    bool     is_directory;      /* True if creating directory */
} __attribute__((packed));

/* ===== Interface Functions ===== */

/**
 * create_ext2 - Initialize new EXT2 filesystem
 */
void create_ext2(void);

/**
 * is_empty_storage - Check if storage is empty (no valid FS)
 * @return true if storage has no valid filesystem
 */
bool is_empty_storage(void);

/**
 * initialize_filesystem_ext2 - Initialize or load filesystem
 */
void initialize_filesystem_ext2(void);

/**
 * read - Read file content
 * @param request Driver request
 * @return Error code (0=success, 1=not file, 2=buffer small, 3=not found, 4=parent invalid)
 */
int8_t read(struct EXT2DriverRequest request);

/**
 * read_directory - Read directory entries
 * @param request Driver request
 * @return Error code (0=success, 1=not folder, 2=not found, 3=parent invalid)
 */
int8_t read_directory(struct EXT2DriverRequest request);

/**
 * write - Write/create file or directory
 * @param request Driver request
 * @return Error code (0=success, 1=name exists, 2=parent invalid)
 */
int8_t write(struct EXT2DriverRequest request);

/**
 * delete - Delete file or directory
 * @param request Driver request
 * @return Error code (0=success, 1=not found, 2=folder not empty, 3=parent invalid)
 */
int8_t delete(struct EXT2DriverRequest request);

#endif