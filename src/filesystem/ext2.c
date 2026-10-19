#include <stdint.h>
#include <stdbool.h>
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

/* ===== State file system di memori (di-commit dengan commit_metadata) ===== */
static struct EXT2Superblock                 ext2_superblock;
static struct EXT2BlockGroupDescriptorTable  ext2_bgd_table;

/* =====================================================================
 *  Helper dasar
 * ===================================================================== */

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1u) / EXT2_INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1u) % EXT2_INODES_PER_GROUP;
}

char *get_entry_name(struct EXT2DirectoryEntry *entry) {
    return (char *) entry + EXT2_DIRENT_HEADER;
}

uint16_t get_entry_record_len(uint8_t name_len) {
    return (uint16_t) ((EXT2_DIRENT_HEADER + name_len + 3u) & ~3u);
}

bool bitmap_test(const uint8_t *bitmap, uint32_t bit) {
    return (bitmap[bit / 8u] & (0x80u >> (bit % 8u))) != 0;
}

void bitmap_set(uint8_t *bitmap, uint32_t bit) {
    bitmap[bit / 8u] = (uint8_t) (bitmap[bit / 8u] | (0x80u >> (bit % 8u)));
}

void bitmap_clear(uint8_t *bitmap, uint32_t bit) {
    bitmap[bit / 8u] = (uint8_t) (bitmap[bit / 8u] & ~(0x80u >> (bit % 8u)));
}

/* Baca 1 block, menolak LBA di luar disk (melindungi dari pointer rusak) */
static bool read_block_checked(void *buf, uint32_t lba) {
    if (lba >= EXT2_TOTAL_BLOCKS)
        return false;
    read_blocks(buf, lba, 1);
    return true;
}

/* =====================================================================
 *  Inode
 * ===================================================================== */

static bool inode_in_use(uint32_t inode) {
    if (inode < 1u || inode > EXT2_INODES_COUNT)
        return false;

    struct BlockBuffer bitmap;
    if (!read_block_checked(&bitmap, ext2_bgd_table.table[inode_to_bgd(inode)].bg_inode_bitmap))
        return false;
    return bitmap_test(bitmap.buf, inode_to_local(inode));
}

bool read_inode(uint32_t inode, struct EXT2Inode *out) {
    if (!inode_in_use(inode))
        return false;

    uint32_t local = inode_to_local(inode);
    uint32_t lba   = ext2_bgd_table.table[inode_to_bgd(inode)].bg_inode_table
                   + local / EXT2_INODES_PER_BLOCK;

    struct BlockBuffer buf;
    if (!read_block_checked(&buf, lba))
        return false;
    memcpy(out, buf.buf + (local % EXT2_INODES_PER_BLOCK) * EXT2_INODE_SIZE, sizeof(struct EXT2Inode));
    return true;
}

bool write_inode(uint32_t inode, const struct EXT2Inode *in) {
    if (inode < 1u || inode > EXT2_INODES_COUNT)
        return false;

    uint32_t local = inode_to_local(inode);
    uint32_t lba   = ext2_bgd_table.table[inode_to_bgd(inode)].bg_inode_table
                   + local / EXT2_INODES_PER_BLOCK;

    struct BlockBuffer buf;
    if (!read_block_checked(&buf, lba))
        return false;
    memcpy(buf.buf + (local % EXT2_INODES_PER_BLOCK) * EXT2_INODE_SIZE, in, sizeof(struct EXT2Inode));
    write_blocks(&buf, lba, 1);
    return true;
}

/* Ambil pointer ke-`index` dari sebuah pointer block */
static uint32_t read_pointer(uint32_t block, uint32_t index) {
    struct BlockBuffer buf;
    uint32_t value;
    if (index >= EXT2_PTRS_PER_BLOCK || !read_block_checked(&buf, block))
        return 0;
    memcpy(&value, buf.buf + index * sizeof(uint32_t), sizeof(uint32_t));
    return value;
}

/* depth 1 = indirect, 2 = doubly-indirect, 3 = triply-indirect */
static uint32_t resolve_indirect(uint32_t block, uint32_t depth, uint32_t index) {
    if (block == 0 || block >= EXT2_TOTAL_BLOCKS)
        return 0;

    uint32_t span = 1;
    for (uint32_t i = 1; i < depth; i++)
        span *= EXT2_PTRS_PER_BLOCK;

    uint32_t entry = read_pointer(block, index / span);
    if (depth == 1)
        return entry;
    return resolve_indirect(entry, depth - 1u, index % span);
}

uint32_t get_inode_block(const struct EXT2Inode *inode, uint32_t index) {
    uint32_t result = 0;

    if (index < EXT2_DIRECT_BLOCKS) {
        result = inode->i_block[index];
    } else {
        uint32_t rest     = index - EXT2_DIRECT_BLOCKS;
        uint32_t capacity = EXT2_PTRS_PER_BLOCK;     /* kapasitas level indirect */
        for (uint32_t depth = 1; depth <= 3; depth++) {
            if (rest < capacity) {
                result = resolve_indirect(inode->i_block[EXT2_DIRECT_BLOCKS - 1u + depth], depth, rest);
                break;
            }
            rest     -= capacity;
            capacity *= EXT2_PTRS_PER_BLOCK;
        }
    }
    return result < EXT2_TOTAL_BLOCKS ? result : 0;
}

/* =====================================================================
 *  Direktori
 * ===================================================================== */

static bool inode_is_dir(const struct EXT2Inode *inode) {
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

static bool inode_is_file(const struct EXT2Inode *inode) {
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG;
}

/* Jumlah block tabel direktori. i_size == 0 -> pakai i_blocks (sample image). */
static uint32_t dir_block_count(const struct EXT2Inode *dir) {
    uint32_t count = (dir->i_size + EXT2_BLOCK_SIZE - 1u) / EXT2_BLOCK_SIZE;
    if (count == 0)
        count = dir->i_blocks;
    return count < EXT2_TOTAL_BLOCKS ? count : EXT2_TOTAL_BLOCKS;
}

bool find_directory_entry(uint32_t dir_inode, const char *name, uint8_t name_len,
                          uint32_t *out_inode, uint8_t *out_type) {
    if (name == NULL || name_len == 0)
        return false;

    struct EXT2Inode dir;
    if (!read_inode(dir_inode, &dir) || !inode_is_dir(&dir))
        return false;

    uint32_t blocks = dir_block_count(&dir);
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t lba = get_inode_block(&dir, b);
        struct BlockBuffer buf;
        if (lba == 0 || !read_block_checked(&buf, lba))
            return false;

        uint32_t offset = 0;
        while (offset + EXT2_DIRENT_HEADER <= EXT2_BLOCK_SIZE) {
            struct EXT2DirectoryEntry entry;
            memcpy(&entry, buf.buf + offset, EXT2_DIRENT_HEADER);

            /* rec_len == 0 : akhir tabel. Nilai tidak masuk akal : tabel rusak. */
            if (entry.rec_len < EXT2_DIRENT_HEADER || offset + entry.rec_len > EXT2_BLOCK_SIZE)
                break;

            if (entry.inode != 0 && entry.name_len == name_len
                && EXT2_DIRENT_HEADER + name_len <= entry.rec_len
                && memcmp(buf.buf + offset + EXT2_DIRENT_HEADER, name, name_len) == 0) {
                if (out_inode != NULL)
                    *out_inode = entry.inode;
                if (out_type != NULL)
                    *out_type = entry.file_type;
                return true;
            }
            offset += entry.rec_len;
        }
    }
    return false;
}

/* =====================================================================
 *  Commit & inisialisasi (2.5)
 * ===================================================================== */

void commit_metadata(void) {
    struct BlockBuffer buf;

    memset(&buf, 0, sizeof(buf));
    memcpy(buf.buf, &ext2_superblock, sizeof(ext2_superblock));
    write_blocks(&buf, EXT2_SUPERBLOCK_BLOCK, 1);

    memset(&buf, 0, sizeof(buf));
    memcpy(buf.buf, &ext2_bgd_table, sizeof(ext2_bgd_table));
    write_blocks(&buf, EXT2_BGD_TABLE_BLOCK, 1);
}

bool is_empty_storage(void) {
    struct BlockBuffer boot;
    read_blocks(&boot, EXT2_BOOT_SECTOR_BLOCK, 1);
    return memcmp(boot.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void) {
    struct BlockBuffer buf;

    /* 1. Boot sector */
    write_blocks(fs_signature, EXT2_BOOT_SECTOR_BLOCK, 1);

    /* 2. Group descriptor, bitmap, dan inode table tiap group */
    memset(&ext2_superblock, 0, sizeof(ext2_superblock));
    memset(&ext2_bgd_table, 0, sizeof(ext2_bgd_table));

    uint32_t total_free_blocks = 0;
    uint32_t total_free_inodes = 0;

    for (uint32_t g = 0; g < EXT2_GROUPS_COUNT; g++) {
        struct EXT2BlockGroupDescriptor *bgd = &ext2_bgd_table.table[g];
        uint32_t group_start = g * EXT2_BLOCKS_PER_GROUP;
        uint32_t meta_blocks = EXT2_GROUP_DATA_START(g) - group_start;   /* g0: 21, lainnya: 18 */

        bgd->bg_block_bitmap      = EXT2_GROUP_BLOCK_BITMAP(g);
        bgd->bg_inode_bitmap      = EXT2_GROUP_INODE_BITMAP(g);
        bgd->bg_inode_table       = EXT2_GROUP_INODE_TABLE(g);
        bgd->bg_free_blocks_count = (uint16_t) (EXT2_BLOCKS_PER_GROUP - meta_blocks);
        bgd->bg_free_inodes_count = EXT2_INODES_PER_GROUP;
        bgd->bg_used_dirs_count   = 0;

        /* Block bitmap: block metadata terpakai */
        memset(&buf, 0, sizeof(buf));
        for (uint32_t i = 0; i < meta_blocks; i++)
            bitmap_set(buf.buf, i);

        /* Group 0 juga memakai block data pertama untuk root directory */
        if (g == 0) {
            bitmap_set(buf.buf, EXT2_GROUP_DATA_START(0));
            bgd->bg_free_blocks_count--;
            bgd->bg_free_inodes_count--;
            bgd->bg_used_dirs_count = 1;
        }
        write_blocks(&buf, bgd->bg_block_bitmap, 1);

        /* Inode bitmap: inode root (bit 0 group 0) terpakai */
        memset(&buf, 0, sizeof(buf));
        if (g == 0)
            bitmap_set(buf.buf, inode_to_local(EXT2_ROOT_INODE));
        write_blocks(&buf, bgd->bg_inode_bitmap, 1);

        /* Inode table dikosongkan agar isi disk lama tidak ikut terbaca */
        memset(&buf, 0, sizeof(buf));
        for (uint32_t t = 0; t < EXT2_INODE_TABLE_BLOCKS; t++)
            write_blocks(&buf, bgd->bg_inode_table + t, 1);

        total_free_blocks += bgd->bg_free_blocks_count;
        total_free_inodes += bgd->bg_free_inodes_count;
    }

    /* 3. Superblock */
    ext2_superblock.s_inodes_count      = EXT2_INODES_COUNT;
    ext2_superblock.s_blocks_count      = EXT2_TOTAL_BLOCKS;
    ext2_superblock.s_r_blocks_count    = 0;
    ext2_superblock.s_free_blocks_count = total_free_blocks;
    ext2_superblock.s_free_inodes_count = total_free_inodes;
    ext2_superblock.s_first_data_block  = EXT2_SUPERBLOCK_BLOCK;
    ext2_superblock.s_first_ino         = EXT2_ROOT_INODE;
    ext2_superblock.s_blocks_per_group  = EXT2_BLOCKS_PER_GROUP;
    ext2_superblock.s_frags_per_group   = EXT2_BLOCKS_PER_GROUP;
    ext2_superblock.s_inodes_per_group  = EXT2_INODES_PER_GROUP;
    ext2_superblock.s_magic             = EXT2_SUPER_MAGIC;

    /* 4. Root directory: inode + block tabel entry ('.' dan '..') */
    uint32_t root_block = EXT2_GROUP_DATA_START(0);

    struct EXT2Inode root;
    memset(&root, 0, sizeof(root));
    root.i_mode        = EXT2_S_IFDIR;
    root.i_size        = EXT2_BLOCK_SIZE;
    root.i_links_count = 2;
    root.i_blocks      = 1;
    root.i_block[0]    = root_block;
    write_inode(EXT2_ROOT_INODE, &root);

    memset(&buf, 0, sizeof(buf));
    struct EXT2DirectoryEntry dot = {
        .inode = EXT2_ROOT_INODE, .rec_len = get_entry_record_len(1),
        .name_len = 1, .file_type = EXT2_FT_DIR
    };
    struct EXT2DirectoryEntry dotdot = {
        .inode = EXT2_ROOT_INODE, .rec_len = get_entry_record_len(2),
        .name_len = 2, .file_type = EXT2_FT_DIR
    };
    memcpy(buf.buf, &dot, EXT2_DIRENT_HEADER);
    memcpy(buf.buf + EXT2_DIRENT_HEADER, ".", 1);
    memcpy(buf.buf + dot.rec_len, &dotdot, EXT2_DIRENT_HEADER);
    memcpy(buf.buf + dot.rec_len + EXT2_DIRENT_HEADER, "..", 2);
    write_blocks(&buf, root_block, 1);

    /* 5. Commit superblock dan BGD table */
    commit_metadata();
}

/* Muat superblock (block 1) dan block group descriptor table (block 2) */
static void load_metadata(void) {
    struct BlockBuffer buf;

    read_blocks(&buf, EXT2_SUPERBLOCK_BLOCK, 1);
    memcpy(&ext2_superblock, buf.buf, sizeof(ext2_superblock));

    read_blocks(&buf, EXT2_BGD_TABLE_BLOCK, 1);
    memcpy(&ext2_bgd_table, buf.buf, sizeof(ext2_bgd_table));
}

void initialize_filesystem_ext2(void) {
    if (is_empty_storage())
        create_ext2();
    else
        load_metadata();
}

/* =====================================================================
 *  CRUD: read dan read_directory (2.6.1)
 * ===================================================================== */

int8_t read(struct EXT2DriverRequest request) {
    struct EXT2Inode parent;
    if (!read_inode(request.parent_inode, &parent) || !inode_is_dir(&parent))
        return 4;

    uint32_t file_inode_number;
    uint8_t  file_type;
    if (!find_directory_entry(request.parent_inode, request.name, request.name_len,
                              &file_inode_number, &file_type))
        return 3;

    if (file_type != EXT2_FT_REG_FILE)
        return 1;

    struct EXT2Inode file;
    if (!read_inode(file_inode_number, &file) || !inode_is_file(&file))
        return -1;

    if (request.buffer_size < file.i_size)
        return 2;
    if (file.i_size > 0 && request.buf == NULL)
        return -1;

    uint8_t *dest      = (uint8_t *) request.buf;
    uint32_t remaining = file.i_size;
    for (uint32_t index = 0; remaining > 0; index++) {
        struct BlockBuffer buf;
        uint32_t lba = get_inode_block(&file, index);
        if (lba == 0 || !read_block_checked(&buf, lba))
            return -1;

        uint32_t chunk = remaining < EXT2_BLOCK_SIZE ? remaining : EXT2_BLOCK_SIZE;
        memcpy(dest + index * EXT2_BLOCK_SIZE, buf.buf, chunk);
        remaining -= chunk;
    }
    return 0;
}

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    if (prequest == NULL)
        return -1;
    struct EXT2DriverRequest request = *prequest;

    struct EXT2Inode parent;
    if (!read_inode(request.parent_inode, &parent) || !inode_is_dir(&parent))
        return 3;

    uint32_t dir_inode_number;
    uint8_t  dir_type;
    if (!find_directory_entry(request.parent_inode, request.name, request.name_len,
                              &dir_inode_number, &dir_type))
        return 2;

    if (dir_type != EXT2_FT_DIR)
        return 1;

    struct EXT2Inode dir;
    if (!read_inode(dir_inode_number, &dir) || !inode_is_dir(&dir))
        return -1;

    uint32_t blocks = dir_block_count(&dir);
    if (request.buf == NULL || request.buffer_size < blocks * EXT2_BLOCK_SIZE)
        return -1;

    uint8_t *dest = (uint8_t *) request.buf;
    for (uint32_t index = 0; index < blocks; index++) {
        struct BlockBuffer buf;
        uint32_t lba = get_inode_block(&dir, index);
        if (lba == 0 || !read_block_checked(&buf, lba))
            return -1;
        memcpy(dest + index * EXT2_BLOCK_SIZE, buf.buf, EXT2_BLOCK_SIZE);
    }
    return 0;
}

/* =====================================================================
 *  TODO bagian lain (stub agar build tetap lengkap)
 * ===================================================================== */

/* TODO(D): tulis file/folder */
int8_t write(struct EXT2DriverRequest *request) {
    (void) request;
    return -1;
}

/* TODO(C): hapus file/folder */
int8_t delete(struct EXT2DriverRequest request) {
    (void) request;
    return -1;
}