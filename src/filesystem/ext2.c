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

/* ===== State file system di memori ===== */
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
    return (uint16_t) ((EXT2_DIRENT_HEADER + name_len + 1u + 3u) & ~3u);   /* +1: NUL penutup */
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

static uint32_t read_pointer(uint32_t block, uint32_t index) {
    struct BlockBuffer buf;
    uint32_t value;
    if (index >= EXT2_PTRS_PER_BLOCK || !read_block_checked(&buf, block))
        return 0;
    memcpy(&value, buf.buf + index * sizeof(uint32_t), sizeof(uint32_t));
    return value;
}

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
        uint32_t capacity = EXT2_PTRS_PER_BLOCK;
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

/* Di sample-image.bin sebagian entry menghitung NUL penutup di name_len
 * (mis. "tpazolite" tercatat 10). Pembandingan nama harus mengabaikan NUL di ujung. */
static uint8_t effective_name_len(const char *name, uint8_t len) {
    while (len > 0 && name[len - 1] == '\0')
        len--;
    return len;
}

static bool inode_is_dir(const struct EXT2Inode *inode) {
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

static bool inode_is_file(const struct EXT2Inode *inode) {
    return (inode->i_mode & EXT2_S_IFMT) == EXT2_S_IFREG;
}

static uint32_t dir_block_count(const struct EXT2Inode *dir) {
    uint32_t count = (dir->i_size + EXT2_BLOCK_SIZE - 1u) / EXT2_BLOCK_SIZE;
    if (count == 0)
        count = dir->i_blocks;
    return count < EXT2_TOTAL_BLOCKS ? count : EXT2_TOTAL_BLOCKS;
}

bool find_directory_entry(uint32_t dir_inode, const char *name, uint8_t name_len,
                          uint32_t *out_inode, uint8_t *out_type) {
    if (name == NULL)
        return false;
    name_len = effective_name_len(name, name_len);
    if (name_len == 0)
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

            if (entry.rec_len < EXT2_DIRENT_HEADER || offset + entry.rec_len > EXT2_BLOCK_SIZE)
                break;

            if (entry.inode != 0
                && EXT2_DIRENT_HEADER + entry.name_len <= entry.rec_len
                && effective_name_len((const char *) buf.buf + offset + EXT2_DIRENT_HEADER, entry.name_len) == name_len
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
 *  Commit & inisialisasi
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

    write_blocks(fs_signature, EXT2_BOOT_SECTOR_BLOCK, 1);

    memset(&ext2_superblock, 0, sizeof(ext2_superblock));
    memset(&ext2_bgd_table, 0, sizeof(ext2_bgd_table));

    uint32_t total_free_blocks = 0;
    uint32_t total_free_inodes = 0;

    for (uint32_t g = 0; g < EXT2_GROUPS_COUNT; g++) {
        struct EXT2BlockGroupDescriptor *bgd = &ext2_bgd_table.table[g];
        uint32_t group_start = g * EXT2_BLOCKS_PER_GROUP;
        uint32_t meta_blocks = EXT2_GROUP_DATA_START(g) - group_start;

        bgd->bg_block_bitmap      = EXT2_GROUP_BLOCK_BITMAP(g);
        bgd->bg_inode_bitmap      = EXT2_GROUP_INODE_BITMAP(g);
        bgd->bg_inode_table       = EXT2_GROUP_INODE_TABLE(g);
        bgd->bg_free_blocks_count = (uint16_t) (EXT2_BLOCKS_PER_GROUP - meta_blocks);
        bgd->bg_free_inodes_count = EXT2_INODES_PER_GROUP;
        bgd->bg_used_dirs_count   = 0;

        memset(&buf, 0, sizeof(buf));
        for (uint32_t i = 0; i < meta_blocks; i++)
            bitmap_set(buf.buf, i);

        if (g == 0) {
            bitmap_set(buf.buf, EXT2_GROUP_DATA_START(0));
            bgd->bg_free_blocks_count--;
            bgd->bg_free_inodes_count--;
            bgd->bg_used_dirs_count = 1;
        }
        write_blocks(&buf, bgd->bg_block_bitmap, 1);

        memset(&buf, 0, sizeof(buf));
        if (g == 0)
            bitmap_set(buf.buf, inode_to_local(EXT2_ROOT_INODE));
        write_blocks(&buf, bgd->bg_inode_bitmap, 1);

        memset(&buf, 0, sizeof(buf));
        for (uint32_t t = 0; t < EXT2_INODE_TABLE_BLOCKS; t++)
            write_blocks(&buf, bgd->bg_inode_table + t, 1);

        total_free_blocks += bgd->bg_free_blocks_count;
        total_free_inodes += bgd->bg_free_inodes_count;
    }

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

    commit_metadata();
}

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
 *  CRUD: read dan read_directory
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
 *  Allocator & Hapus (Bagian C)
 * ===================================================================== */

static uint32_t bitmap_find_first_zero(const uint8_t *bm, uint32_t nbits) {
    for (uint32_t byte = 0; byte < (nbits + 7u) / 8u; byte++) {
        if (bm[byte] == 0xFF) continue;
        for (uint32_t bit = 0; bit < 8u; bit++) {
            uint32_t idx = byte * 8u + bit;
            if (idx >= nbits) return nbits;
            if ((bm[byte] & (0x80u >> bit)) == 0) return idx;
        }
    }
    return nbits;
}

uint32_t allocate_block(uint32_t preferred_bgd) {
    for (uint32_t k = 0; k < EXT2_GROUPS_COUNT; k++) {
        uint32_t g = (preferred_bgd + k) % EXT2_GROUPS_COUNT;
        struct EXT2BlockGroupDescriptor *bgd = &ext2_bgd_table.table[g];
        if (bgd->bg_free_blocks_count == 0) continue;

        struct BlockBuffer bitmap;
        read_blocks(&bitmap, bgd->bg_block_bitmap, 1);

        uint32_t bit = bitmap_find_first_zero(bitmap.buf, EXT2_BLOCKS_PER_GROUP);
        if (bit >= EXT2_BLOCKS_PER_GROUP) continue;

        bitmap_set(bitmap.buf, bit);
        write_blocks(&bitmap, bgd->bg_block_bitmap, 1);

        bgd->bg_free_blocks_count--;
        if (ext2_superblock.s_free_blocks_count > 0) ext2_superblock.s_free_blocks_count--;
        return g * EXT2_BLOCKS_PER_GROUP + bit;
    }
    return 0;
}

bool free_block(uint32_t block) {
    if (block == 0 || block >= EXT2_TOTAL_BLOCKS) return false;

    uint32_t g   = block / EXT2_BLOCKS_PER_GROUP;
    uint32_t bit = block % EXT2_BLOCKS_PER_GROUP;
    if (block < EXT2_GROUP_DATA_START(g)) return false;

    struct EXT2BlockGroupDescriptor *bgd = &ext2_bgd_table.table[g];
    struct BlockBuffer bitmap;
    read_blocks(&bitmap, bgd->bg_block_bitmap, 1);
    if (!bitmap_test(bitmap.buf, bit)) return false;

    bitmap_clear(bitmap.buf, bit);
    write_blocks(&bitmap, bgd->bg_block_bitmap, 1);

    bgd->bg_free_blocks_count++;
    ext2_superblock.s_free_blocks_count++;
    return true;
}

uint32_t allocate_inode(uint32_t preferred_bgd, bool is_dir) {
    for (uint32_t k = 0; k < EXT2_GROUPS_COUNT; k++) {
        uint32_t g = (preferred_bgd + k) % EXT2_GROUPS_COUNT;
        struct EXT2BlockGroupDescriptor *bgd = &ext2_bgd_table.table[g];
        if (bgd->bg_free_inodes_count == 0) continue;

        struct BlockBuffer bitmap;
        read_blocks(&bitmap, bgd->bg_inode_bitmap, 1);

        uint32_t bit = bitmap_find_first_zero(bitmap.buf, EXT2_INODES_PER_GROUP);
        if (bit >= EXT2_INODES_PER_GROUP) continue;

        bitmap_set(bitmap.buf, bit);
        write_blocks(&bitmap, bgd->bg_inode_bitmap, 1);

        bgd->bg_free_inodes_count--;
        if (is_dir) bgd->bg_used_dirs_count++;
        if (ext2_superblock.s_free_inodes_count > 0) ext2_superblock.s_free_inodes_count--;

        uint32_t inode = g * EXT2_INODES_PER_GROUP + bit + 1u;
        struct EXT2Inode empty;
        memset(&empty, 0, sizeof(empty));
        write_inode(inode, &empty);
        return inode;
    }
    return 0;
}

bool free_inode(uint32_t inode) {
    if (inode < 1u || inode > EXT2_INODES_COUNT || inode == EXT2_ROOT_INODE) return false;

    uint32_t g   = inode_to_bgd(inode);
    uint32_t bit = inode_to_local(inode);
    struct EXT2BlockGroupDescriptor *bgd = &ext2_bgd_table.table[g];

    struct BlockBuffer bitmap;
    read_blocks(&bitmap, bgd->bg_inode_bitmap, 1);
    if (!bitmap_test(bitmap.buf, bit)) return false;

    struct EXT2Inode node;
    if (read_inode(inode, &node) && inode_is_dir(&node) && bgd->bg_used_dirs_count > 0)
        bgd->bg_used_dirs_count--;

    bitmap_clear(bitmap.buf, bit);
    write_blocks(&bitmap, bgd->bg_inode_bitmap, 1);

    bgd->bg_free_inodes_count++;
    ext2_superblock.s_free_inodes_count++;

    memset(&node, 0, sizeof(node));
    write_inode(inode, &node);
    return true;
}

static void free_indirect(uint32_t block, uint32_t depth) {
    if (block == 0) return;
    uint32_t ptrs[EXT2_PTRS_PER_BLOCK];
    struct BlockBuffer buf;
    read_blocks(&buf, block, 1);
    memcpy(ptrs, buf.buf, EXT2_BLOCK_SIZE);

    for (uint32_t i = 0; i < EXT2_PTRS_PER_BLOCK; i++) {
        if (ptrs[i] == 0) continue;
        if (depth > 1u) free_indirect(ptrs[i], depth - 1u);
        else free_block(ptrs[i]);
    }
    free_block(block);
}

static void free_inode_blocks(struct EXT2Inode *node) {
    for (uint32_t i = 0; i < EXT2_DIRECT_BLOCKS; i++) {
        if (node->i_block[i] != 0) free_block(node->i_block[i]);
        node->i_block[i] = 0;
    }
    free_indirect(node->i_block[12], 1u);
    node->i_block[12] = 0;
    free_indirect(node->i_block[13], 2u);
    node->i_block[13] = 0;
    free_indirect(node->i_block[14], 3u);
    node->i_block[14] = 0;
    node->i_blocks = 0;
}

static bool is_directory_empty(uint32_t inode) {
    struct EXT2Inode dir;
    if (!read_inode(inode, &dir) || !inode_is_dir(&dir)) return false;

    uint32_t blocks = (dir.i_size + EXT2_BLOCK_SIZE - 1u) / EXT2_BLOCK_SIZE;
    if (blocks == 0) blocks = dir.i_blocks;

    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t lba = get_inode_block(&dir, b);
        if (lba == 0) continue;

        struct BlockBuffer buf;
        read_blocks(&buf, lba, 1);

        uint32_t offset = 0;
        while (offset + EXT2_DIRENT_HEADER <= EXT2_BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(buf.buf + offset);
            if (e->rec_len < EXT2_DIRENT_HEADER || offset + e->rec_len > EXT2_BLOCK_SIZE) break;

            if (e->inode != 0) {
                char *name = get_entry_name(e);
                bool is_dot = (e->name_len == 1 && name[0] == '.');
                bool is_dotdot = (e->name_len == 2 && name[0] == '.' && name[1] == '.');
                if (!is_dot && !is_dotdot) return false;
            }
            offset += e->rec_len;
        }
    }
    return true;
}

static bool remove_directory_entry(uint32_t parent_inode, uint32_t target_inode) {
    struct EXT2Inode parent;
    if (!read_inode(parent_inode, &parent) || !inode_is_dir(&parent)) return false;

    uint32_t blocks = (parent.i_size + EXT2_BLOCK_SIZE - 1u) / EXT2_BLOCK_SIZE;
    if (blocks == 0) blocks = parent.i_blocks;

    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t lba = get_inode_block(&parent, b);
        if (lba == 0) continue;

        struct BlockBuffer buf;
        read_blocks(&buf, lba, 1);

        struct EXT2DirectoryEntry *prev = NULL;
        uint32_t offset = 0;

        while (offset + EXT2_DIRENT_HEADER <= EXT2_BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(buf.buf + offset);
            if (e->rec_len < EXT2_DIRENT_HEADER || offset + e->rec_len > EXT2_BLOCK_SIZE) break;

            if (e->inode == target_inode) {
                if (prev == NULL) e->inode = 0;
                else {
                    prev->rec_len += e->rec_len;
                    e->inode = 0;
                }
                write_blocks(&buf, lba, 1);
                return true;
            }
            prev = e;
            offset += e->rec_len;
        }
    }
    return false;
}

int8_t delete(struct EXT2DriverRequest request) {
    struct EXT2Inode parent;
    if (!read_inode(request.parent_inode, &parent) || !inode_is_dir(&parent)) return 3;

    if (request.name == NULL || request.name_len == 0) return 1;

    uint8_t req_len = effective_name_len(request.name, request.name_len);
    if (req_len == 0) return 1;
    bool is_dot = (req_len == 1 && request.name[0] == '.');
    bool is_dotdot = (req_len == 2 && request.name[0] == '.' && request.name[1] == '.');
    if (is_dot || is_dotdot) return -1;

    uint32_t target_inode;
    uint8_t target_type;
    if (!find_directory_entry(request.parent_inode, request.name, request.name_len, &target_inode, &target_type))
        return 1;

    if (target_inode == EXT2_ROOT_INODE) return -1;

    struct EXT2Inode node;
    if (!read_inode(target_inode, &node)) return -1;

    if (request.is_directory) {
        if (!inode_is_dir(&node)) return 1; 
        if (!is_directory_empty(target_inode)) return 2; 
    } else {
        if (inode_is_dir(&node)) return 1;   /* minta hapus file, tapi namanya folder: tidak ada file itu */
    }

    if (!remove_directory_entry(request.parent_inode, target_inode)) return -1;

    free_inode_blocks(&node);
    free_inode(target_inode);
    commit_metadata();
    
    return 0;
}

/* TODO(D): tulis file/folder */
int8_t write(struct EXT2DriverRequest *request) {
    if (request == NULL) return -1;
    
    struct EXT2Inode parent;
    if (!read_inode(request->parent_inode, &parent)) return 2;
    if ((parent.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return 2; 

    uint32_t existing_inode;
    if (find_directory_entry(request->parent_inode, request->name, request->name_len, &existing_inode, NULL)) {
        return 1; 
    }
    
    uint32_t preferred_bgd = inode_to_bgd(request->parent_inode);
    uint32_t new_inode_num = allocate_inode(preferred_bgd, request->is_directory);
    if (new_inode_num == 0) return -1; 

    struct EXT2Inode new_inode;
    memset(&new_inode, 0, sizeof(new_inode));
    new_inode.i_mode = request->is_directory ? EXT2_S_IFDIR : EXT2_S_IFREG;
    new_inode.i_links_count = request->is_directory ? 2 : 1;
    new_inode.i_size = request->is_directory ? EXT2_BLOCK_SIZE : request->buffer_size;
 
    uint32_t blocks_needed = request->is_directory ? 1 : (request->buffer_size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    if (blocks_needed > EXT2_DIRECT_BLOCKS) {
        
        
        return -1; 
    }

    uint8_t *src_buf = (uint8_t *)request->buf;
    uint32_t remaining = request->buffer_size;

    for (uint32_t i = 0; i < blocks_needed; i++) {
        uint32_t lba = allocate_block(preferred_bgd);
        if (lba == 0) return -1;
        
        new_inode.i_block[i] = lba;
        new_inode.i_blocks++;

        if (request->is_directory) {
            struct BlockBuffer dir_buf;
            memset(&dir_buf, 0, sizeof(dir_buf));
            
            
            struct EXT2DirectoryEntry dot = {
                .inode = new_inode_num, .rec_len = get_entry_record_len(1),
                .name_len = 1, .file_type = EXT2_FT_DIR
            };
            
            struct EXT2DirectoryEntry dotdot = {
                .inode = request->parent_inode, .rec_len = get_entry_record_len(2),
                .name_len = 2, .file_type = EXT2_FT_DIR
            };
            
            memcpy(dir_buf.buf, &dot, EXT2_DIRENT_HEADER);
            memcpy(dir_buf.buf + EXT2_DIRENT_HEADER, ".", 1);
            memcpy(dir_buf.buf + dot.rec_len, &dotdot, EXT2_DIRENT_HEADER);
            memcpy(dir_buf.buf + dot.rec_len + EXT2_DIRENT_HEADER, "..", 2);
            
            write_blocks(&dir_buf, lba, 1);
        } else {
            struct BlockBuffer file_buf;
            memset(&file_buf, 0, sizeof(file_buf));
            uint32_t chunk_size = remaining < EXT2_BLOCK_SIZE ? remaining : EXT2_BLOCK_SIZE;
            
            if (chunk_size > 0 && src_buf != NULL) {
                memcpy(file_buf.buf, src_buf + (i * EXT2_BLOCK_SIZE), chunk_size);
                remaining -= chunk_size;
            }
            write_blocks(&file_buf, lba, 1);
        }
    }
    write_inode(new_inode_num, &new_inode);

    uint32_t parent_blocks = (parent.i_size + EXT2_BLOCK_SIZE - 1) / EXT2_BLOCK_SIZE;
    if (parent_blocks == 0) parent_blocks = parent.i_blocks;
    
    uint16_t req_rec_len = get_entry_record_len(request->name_len);
    bool entry_added = false;
    
    for (uint32_t b = 0; b < parent_blocks && !entry_added; b++) {
        uint32_t lba = get_inode_block(&parent, b);
        if (lba == 0) continue;
        
        struct BlockBuffer pbuf;
        read_blocks(&pbuf, lba, 1);
        
        uint32_t offset = 0;
        while (offset + EXT2_DIRENT_HEADER <= EXT2_BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(pbuf.buf + offset);
            
            if (e->rec_len < EXT2_DIRENT_HEADER) {
                e->rec_len = EXT2_BLOCK_SIZE - offset;
                e->inode = 0; // Pastikan statusnya dihapus/kosong
            }

            if (e->inode == 0) {
                
                if (e->rec_len >= req_rec_len) {
                    e->inode = new_inode_num;
                    e->name_len = request->name_len;
                    e->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
                    memcpy(pbuf.buf + offset + EXT2_DIRENT_HEADER, request->name, request->name_len);
                    write_blocks(&pbuf, lba, 1);
                    entry_added = true;
                    break;
                }
            } else {
                
                uint16_t actual_len = get_entry_record_len(e->name_len);
                if (e->rec_len >= actual_len + req_rec_len) {
                    uint16_t total_len = e->rec_len;
                    e->rec_len = actual_len; 
                    
                    struct EXT2DirectoryEntry *new_e = (struct EXT2DirectoryEntry *)(pbuf.buf + offset + actual_len);
                    new_e->inode = new_inode_num;
                    new_e->rec_len = total_len - actual_len; 
                    new_e->name_len = request->name_len;
                    new_e->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
                    memcpy(pbuf.buf + offset + actual_len + EXT2_DIRENT_HEADER, request->name, request->name_len);
                    
                    write_blocks(&pbuf, lba, 1);
                    entry_added = true;
                    break;
                }
            }
            offset += e->rec_len;
        }
    }

    
    if (!entry_added) {
        if (parent_blocks >= EXT2_DIRECT_BLOCKS) return -1; 
        
        uint32_t new_lba = allocate_block(preferred_bgd);
        if (new_lba == 0) return -1;
        
        parent.i_block[parent_blocks] = new_lba;
        parent.i_blocks++;
        parent.i_size += EXT2_BLOCK_SIZE;
        
        struct BlockBuffer pbuf;
        memset(&pbuf, 0, sizeof(pbuf));
        struct EXT2DirectoryEntry *new_e = (struct EXT2DirectoryEntry *)pbuf.buf;
        new_e->inode = new_inode_num;
        new_e->rec_len = EXT2_BLOCK_SIZE;
        new_e->name_len = request->name_len;
        new_e->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
        memcpy(pbuf.buf + EXT2_DIRENT_HEADER, request->name, request->name_len);
        
        write_blocks(&pbuf, new_lba, 1);
    }

    write_inode(request->parent_inode, &parent);
    commit_metadata();
    
    return 0;
}