#ifndef _EXT2_H
#define _EXT2_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/driver/disk.h"   /* BLOCK_SIZE, struct BlockBuffer, read_blocks, write_blocks */

/* =====================================================================
 *  EXT2 - IF2130 Edition : LAYOUT (keputusan Bagian B)
 *
 *  Disk 4 MiB, block 512 byte, 8 block group x 1024 block.
 *
 *  Block 0            : boot sector (fs_signature)
 *  Block 1            : superblock
 *  Block 2            : block group descriptor table (8 x 32 byte)
 *  Group 0            : block 3  = block bitmap
 *                       block 4  = inode bitmap
 *                       block 5..20 = inode table (16 block)
 *                       block 21.. = data   (block 21 = data root dir)
 *  Group g (g >= 1)   : block g*1024     = block bitmap
 *                       block g*1024 + 1 = inode bitmap
 *                       block g*1024 + 2 .. +17 = inode table
 *                       block g*1024 + 18 .. = data
 *
 *  - Inode 128 byte -> 4 inode / block -> 64 inode / group -> 512 inode total.
 *  - Nomor inode dimulai dari 1. Root = inode 1.
 *    Inode n berada di group (n-1)/64, index lokal (n-1)%64.
 *  - Bitmap: MSB-first (bit 0 = bit paling kiri byte 0), sama dengan sample image.
 *    Bit i pada block bitmap group g mewakili block g*1024 + i
 *    (block metadata ikut ditandai terpakai). Bit i pada inode bitmap group g
 *    mewakili inode lokal i (nomor inode = g*64 + i + 1).
 *  - Directory entry: header 8 byte (inode, rec_len, name_len, file_type) diikuti
 *    nama (tanpa NUL). rec_len = align4(8 + name_len). Entry terakhir berakhir
 *    saat rec_len == 0 atau offset mencapai akhir block. inode == 0 = entry kosong.
 *  - i_block: 12 direct, [12] indirect, [13] doubly-indirect, [14] triply-indirect.
 * ===================================================================== */

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

/* Parameter untuk semua interface CRUD */
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

/* Inode 128 byte. Offset i_size = 2, i_blocks = 28, i_block = 40 (sama dengan sample image). */
struct EXT2Inode {
    uint16_t i_mode;          /* offset   0 : EXT2_S_IFREG / EXT2_S_IFDIR          */
    uint32_t i_size;          /* offset   2 : ukuran dalam byte                    */
    uint32_t i_atime;         /* offset   6 : (tidak dipakai)                      */
    uint32_t i_ctime;         /* offset  10 : (tidak dipakai)                      */
    uint32_t i_mtime;         /* offset  14 : (tidak dipakai)                      */
    uint32_t i_dtime;         /* offset  18 : (tidak dipakai)                      */
    uint16_t i_gid;           /* offset  22 : (tidak dipakai)                      */
    uint16_t i_links_count;   /* offset  24                                        */
    uint16_t i_pad;           /* offset  26 : padding                              */
    uint32_t i_blocks;        /* offset  28 : jumlah block 512-byte milik inode    */
    uint32_t i_flags;         /* offset  32 : (tidak dipakai)                      */
    uint32_t i_osd1;          /* offset  36 : (tidak dipakai)                      */
    uint32_t i_block[15];     /* offset  40 : 12 direct + indirect + 2x + 3x       */
    uint8_t  i_reserved[28];  /* offset 100 : padding sampai 128 byte              */
} __attribute__((packed));

/* Header directory entry (8 byte). Nama berada tepat setelah header. */
struct EXT2DirectoryEntry {
    uint32_t inode;       /* 0 = entry kosong */
    uint16_t rec_len;     /* ukuran total entry, kelipatan 4 */
    uint8_t  name_len;
    uint8_t  file_type;   /* EXT2_FT_* */
} __attribute__((packed));

_Static_assert(sizeof(struct EXT2Inode) == EXT2_INODE_SIZE, "EXT2Inode harus 128 byte");
_Static_assert(sizeof(struct EXT2BlockGroupDescriptor) == 32, "BGD harus 32 byte");
_Static_assert(sizeof(struct EXT2BlockGroupDescriptorTable) <= BLOCK_SIZE, "BGD table harus muat 1 block");
_Static_assert(sizeof(struct EXT2Superblock) <= BLOCK_SIZE, "Superblock harus muat 1 block");
_Static_assert(sizeof(struct EXT2DirectoryEntry) == EXT2_DIRENT_HEADER, "Header dirent harus 8 byte");

/* 512 byte boot sector, isi sama dengan sample image di kit */
extern const uint8_t fs_signature[BLOCK_SIZE];

/* ===== Helper umum (dipakai bagian lain: allocator, write, delete) ===== */

/* Nomor inode -> group dan index lokal (inode dimulai dari 1) */
uint32_t inode_to_bgd(uint32_t inode);
uint32_t inode_to_local(uint32_t inode);

/* Directory entry */
char    *get_entry_name(struct EXT2DirectoryEntry *entry);
uint16_t get_entry_record_len(uint8_t name_len);   /* align4(8 + name_len) */

/* Bitmap (MSB-first) */
bool bitmap_test(const uint8_t *bitmap, uint32_t bit);
void bitmap_set(uint8_t *bitmap, uint32_t bit);
void bitmap_clear(uint8_t *bitmap, uint32_t bit);

/* Inode. read_inode() gagal (false) jika inode di luar 1..512 atau bit inode bitmap = 0 */
bool read_inode(uint32_t inode, struct EXT2Inode *out);
bool write_inode(uint32_t inode, const struct EXT2Inode *in);

/* Block ke-index dari data sebuah inode (direct/indirect/doubly/triply). 0 = tidak ada */
uint32_t get_inode_block(const struct EXT2Inode *inode, uint32_t index);

/* Cari entry bernama `name` di dalam direktori `dir_inode` (semua block direktori) */
bool find_directory_entry(uint32_t dir_inode, const char *name, uint8_t name_len,
                          uint32_t *out_inode, uint8_t *out_type);

/* Tulis superblock dan BGD table (state di memori) ke disk */
void commit_metadata(void);

/* ===== Interface wajib ===== */
bool is_empty_storage(void);
void create_ext2(void);
void initialize_filesystem_ext2(void);

/**
 * read: membaca file `name` di folder `parent_inode` ke `buf`.
 *  0 sukses | 1 bukan file | 2 buffer tidak cukup | 3 file tidak ditemukan
 *  4 parent invalid | -1 error lain
 */
int8_t read(struct EXT2DriverRequest request);

/**
 * read_directory: membaca seluruh tabel entry folder `name` ke `buf`
 * (jumlah block folder x 512 byte).
 *  0 sukses | 1 bukan folder | 2 folder tidak ditemukan | 3 parent invalid
 *  -1 error lain (termasuk buffer < ukuran tabel folder)
 */
int8_t read_directory(struct EXT2DriverRequest *prequest);

/**
 * write: menulis file/folder.  (BAGIAN D)
 *  0 sukses | 1 nama sudah ada | 2 parent invalid | -1 error lain
 */
int8_t write(struct EXT2DriverRequest *request);

/**
 * delete: menghapus file/folder.  (BAGIAN C)
 *  0 sukses | 1 tidak ditemukan | 2 folder tidak kosong | 3 parent invalid | -1 error lain
 */
int8_t delete(struct EXT2DriverRequest request);

#endif