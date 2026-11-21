/*
 * konOSuba shell - user mode (Chapter 3, bagian D).
 *
 * ABI syscall mengikuti buku IF2130 edisi 4, bagian 3.4.1.1:
 *   eax = nomor layanan, ebx/ecx/edx = argumen.
 *   0 read(request*, int8_t* retcode), 1 read_directory(...), 2 write(...),
 *   3 delete(...), 4 getchar(char*), 5 putchar(char, color),
 *   6 puts(char*, count, color), 7 keyboard activate.
 */

#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"

#define SYS_READ                 0
#define SYS_READ_DIRECTORY       1
#define SYS_WRITE                2
#define SYS_DELETE               3
#define SYS_GETCHAR              4
#define SYS_PUTCHAR              5
#define SYS_PUTS                 6
#define SYS_KEYBOARD_ACTIVATE    7

#define COLOR_DEFAULT  0x07
#define COLOR_PATH     0x0B
#define COLOR_ERROR    0x0C

#define LINE_MAX        256
#define PATH_MAX_LEN    256
#define NAME_MAX_LEN    255
#define ARG_MAX         16
#define STAGE_MAX       8
#define MOVE_DEPTH_MAX  16

#define DIR_BUFFER_SIZE   (64u * EXT2_BLOCK_SIZE)
#define FILE_BUFFER_SIZE  (128u * EXT2_BLOCK_SIZE)
#define IO_BUFFER_SIZE    FILE_BUFFER_SIZE
#define FIND_QUEUE_MAX    128

struct PathRef {
    uint32_t parent_inode;
    uint32_t inode;
    uint8_t  type;
    uint8_t  name_len;
    char     name[NAME_MAX_LEN + 1];
};

struct EntryInfo {
    uint32_t inode;
    uint8_t  type;
    uint8_t  name_len;
    char     name[NAME_MAX_LEN + 1];
};

struct Output {
    char     *buf;
    uint32_t  len;
    uint32_t  cap;
    bool      truncated;
};

struct FindNode {
    uint32_t inode;
    char     path[PATH_MAX_LEN];
};

static uint32_t cwd_inode = EXT2_ROOT_INODE;
static char     cwd_path[PATH_MAX_LEN] = "/";

static uint8_t dir_buffer[DIR_BUFFER_SIZE];
static uint8_t file_buffer[FILE_BUFFER_SIZE];
static char    io_buffer[2][IO_BUFFER_SIZE];
static char    line_buffer[LINE_MAX];
static struct FindNode find_queue[FIND_QUEUE_MAX];

static int32_t syscall(uint32_t number, uint32_t a1, uint32_t a2, uint32_t a3) {
    int32_t result;
    __asm__ volatile(
        "int $0x30"
        : "=a"(result)
        : "a"(number), "b"(a1), "c"(a2), "d"(a3)
        : "memory", "cc"
    );
    return result;
}

/* ---------- string kecil ---------- */

static uint32_t s_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int s_cmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void s_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (cap == 0) return;
    while (src[i] != '\0' && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool s_eq_n(const char *a, const char *b, uint32_t n) {
    return memcmp(a, b, n) == 0;
}

/* ---------- buffer output ---------- */

static void out_init(struct Output *out, char *buf, uint32_t cap) {
    out->buf = buf;
    out->len = 0;
    out->cap = cap;
    out->truncated = false;
}

static void out_char(struct Output *out, char c) {
    if (out->len >= out->cap) {
        out->truncated = true;
        return;
    }
    out->buf[out->len++] = c;
}

static void out_str_n(struct Output *out, const char *s, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) out_char(out, s[i]);
}

static void out_str(struct Output *out, const char *s) {
    out_str_n(out, s, s_len(s));
}

static void out_u32(struct Output *out, uint32_t value) {
    char digits[10];
    uint32_t count = 0;
    if (value == 0) {
        out_char(out, '0');
        return;
    }
    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) out_char(out, digits[--count]);
}

static void emit(const char *s, uint32_t n, uint8_t color) {
    while (n > 0) {
        uint32_t chunk = n < 2048u ? n : 2048u;
        syscall(SYS_PUTS, (uint32_t)s, chunk, color);
        s += chunk;
        n -= chunk;
    }
}

/* ---------- filesystem lewat syscall ---------- */

static int8_t fs_read(uint32_t parent, const char *name, uint8_t name_len,
                      void *buf, uint32_t size) {
    struct EXT2DriverRequest request;
    int32_t retcode = (int32_t)0x55555555;

    request.buf = buf;
    request.name = (char *)name;
    request.name_len = name_len;
    request.parent_inode = parent;
    request.buffer_size = size;
    request.is_directory = false;

    int32_t eax = syscall(SYS_READ, (uint32_t)&request, (uint32_t)&retcode, 0);
    if (retcode != (int32_t)0x55555555) return (int8_t)retcode;
    return (int8_t)eax;
}

static int8_t fs_read_dir(uint32_t inode) {
    struct EXT2DriverRequest request;
    int32_t retcode = (int32_t)0x55555555;

    memset(dir_buffer, 0, DIR_BUFFER_SIZE);

    request.buf = dir_buffer;
    request.name = ".";
    request.name_len = 1;
    request.parent_inode = inode;
    request.buffer_size = DIR_BUFFER_SIZE;
    request.is_directory = true;

    int32_t eax = syscall(SYS_READ_DIRECTORY, (uint32_t)&request, (uint32_t)&retcode, 0);
    if (retcode != (int32_t)0x55555555) return (int8_t)retcode;
    return (int8_t)eax;
}

static int8_t fs_write(uint32_t parent, const char *name, uint8_t name_len,
                       const void *buf, uint32_t size, bool is_directory) {
    struct EXT2DriverRequest request;
    int32_t retcode = (int32_t)0x55555555;

    request.buf = (void *)buf;
    request.name = (char *)name;
    request.name_len = name_len;
    request.parent_inode = parent;
    request.buffer_size = size;
    request.is_directory = is_directory;

    int32_t eax = syscall(SYS_WRITE, (uint32_t)&request, (uint32_t)&retcode, 0);
    if (retcode != (int32_t)0x55555555) return (int8_t)retcode;
    return (int8_t)eax;
}

static int8_t fs_delete(uint32_t parent, const char *name, uint8_t name_len,
                        bool is_directory) {
    struct EXT2DriverRequest request;
    int32_t retcode = (int32_t)0x55555555;

    request.buf = NULL;
    request.name = (char *)name;
    request.name_len = name_len;
    request.parent_inode = parent;
    request.buffer_size = 0;
    request.is_directory = is_directory;

    int32_t eax = syscall(SYS_DELETE, (uint32_t)&request, (uint32_t)&retcode, 0);
    if (retcode != (int32_t)0x55555555) return (int8_t)retcode;
    return (int8_t)eax;
}

/* ---------- pembacaan direktori ---------- */

static bool dir_next(uint32_t *offset, struct EntryInfo *out) {
    while (*offset + EXT2_DIRENT_HEADER <= DIR_BUFFER_SIZE) {
        uint32_t off = *offset;
        struct EXT2DirectoryEntry entry;
        const char *entry_name;
        uint32_t name_len;

        memcpy(&entry, dir_buffer + off, EXT2_DIRENT_HEADER);
        if (entry.rec_len < EXT2_DIRENT_HEADER || off + entry.rec_len > DIR_BUFFER_SIZE)
            return false;

        *offset = off + entry.rec_len;

        if (entry.inode == 0) continue;
        if (EXT2_DIRENT_HEADER + entry.name_len > entry.rec_len) continue;

        entry_name = (const char *)(dir_buffer + off + EXT2_DIRENT_HEADER);
        name_len = entry.name_len;
        while (name_len > 0 && entry_name[name_len - 1] == '\0') name_len--;
        if (name_len == 0 || name_len > NAME_MAX_LEN) continue;

        out->inode = entry.inode;
        out->type = entry.file_type;
        out->name_len = (uint8_t)name_len;
        memcpy(out->name, entry_name, name_len);
        out->name[name_len] = '\0';
        return true;
    }
    return false;
}

static bool is_dot_entry(const struct EntryInfo *entry) {
    if (entry->name_len == 1 && entry->name[0] == '.') return true;
    if (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.') return true;
    return false;
}

static bool lookup_child(uint32_t dir_inode, const char *name, uint8_t name_len,
                         uint32_t *out_inode, uint8_t *out_type) {
    uint32_t offset = 0;
    struct EntryInfo entry;

    if (fs_read_dir(dir_inode) != 0) return false;

    while (dir_next(&offset, &entry)) {
        if (entry.name_len == name_len && s_eq_n(entry.name, name, name_len)) {
            if (out_inode != NULL) *out_inode = entry.inode;
            if (out_type != NULL) *out_type = entry.type;
            return true;
        }
    }
    return false;
}

static bool get_nth_entry(uint32_t dir_inode, uint32_t index, struct EntryInfo *out) {
    uint32_t offset = 0;
    uint32_t seen = 0;
    struct EntryInfo entry;

    if (fs_read_dir(dir_inode) != 0) return false;

    while (dir_next(&offset, &entry)) {
        if (is_dot_entry(&entry)) continue;
        if (seen == index) {
            *out = entry;
            return true;
        }
        seen++;
    }
    return false;
}

/* ---------- path ---------- */

static bool path_push(char *path, const char *name, uint32_t name_len) {
    uint32_t len = s_len(path);
    bool need_slash = (len == 0 || path[len - 1] != '/');

    if (len + (need_slash ? 1u : 0u) + name_len + 1u > PATH_MAX_LEN) return false;
    if (need_slash) path[len++] = '/';
    memcpy(path + len, name, name_len);
    path[len + name_len] = '\0';
    return true;
}

static void path_pop(char *path) {
    uint32_t len = s_len(path);

    if (len <= 1) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    while (len > 0 && path[len - 1] != '/') len--;
    if (len == 0) {
        path[0] = '/';
        path[1] = '\0';
    } else if (len == 1) {
        path[1] = '\0';
    } else {
        path[len - 1] = '\0';
    }
}

static int resolve_entry(const char *path, struct PathRef *out) {
    uint32_t current;
    const char *cursor;
    bool found = false;

    if (path == NULL || path[0] == '\0') return -1;

    current = path[0] == '/' ? EXT2_ROOT_INODE : cwd_inode;
    cursor = path;
    while (*cursor == '/') cursor++;

    if (*cursor == '\0') {
        out->parent_inode = EXT2_ROOT_INODE;
        out->inode = EXT2_ROOT_INODE;
        out->type = EXT2_FT_DIR;
        out->name_len = 1;
        s_copy(out->name, ".", sizeof(out->name));
        return 0;
    }

    while (*cursor != '\0') {
        const char *start = cursor;
        uint32_t len;

        while (*cursor != '\0' && *cursor != '/') cursor++;
        len = (uint32_t)(cursor - start);
        while (*cursor == '/') cursor++;

        if (len == 0) continue;

        if (len == 1 && start[0] == '.') {
            out->parent_inode = current;
            out->inode = current;
            out->type = EXT2_FT_DIR;
            out->name_len = 1;
            s_copy(out->name, ".", sizeof(out->name));
            found = true;
            continue;
        }

        if (len == 2 && start[0] == '.' && start[1] == '.') {
            uint32_t parent = current;
            if (current != EXT2_ROOT_INODE
                && !lookup_child(current, "..", 2, &parent, NULL))
                return -1;
            out->parent_inode = current;
            out->inode = parent;
            out->type = EXT2_FT_DIR;
            out->name_len = 2;
            s_copy(out->name, "..", sizeof(out->name));
            current = parent;
            found = true;
            continue;
        }

        if (len > NAME_MAX_LEN) return -1;

        {
            uint32_t inode;
            uint8_t type;
            if (!lookup_child(current, start, (uint8_t)len, &inode, &type)) return -1;

            out->parent_inode = current;
            out->inode = inode;
            out->type = type;
            out->name_len = (uint8_t)len;
            memcpy(out->name, start, len);
            out->name[len] = '\0';
            current = inode;
            found = true;
        }
    }

    return found ? 0 : -1;
}

static int resolve_parent(const char *path, uint32_t *parent_inode,
                          char *name, uint8_t *name_len) {
    const char *last_slash = NULL;
    const char *base;
    const char *cursor;
    uint32_t base_len;

    if (path == NULL || path[0] == '\0') return -1;
    if (path[s_len(path) - 1] == '/') return -1;

    for (cursor = path; *cursor != '\0'; cursor++)
        if (*cursor == '/') last_slash = cursor;

    base = last_slash == NULL ? path : last_slash + 1;
    base_len = s_len(base);
    if (base_len == 0 || base_len > NAME_MAX_LEN) return -1;
    if (base_len == 1 && base[0] == '.') return -1;
    if (base_len == 2 && base[0] == '.' && base[1] == '.') return -1;

    if (last_slash == NULL) {
        *parent_inode = cwd_inode;
    } else {
        uint32_t prefix_len = (uint32_t)(last_slash - path);
        if (prefix_len == 0) {
            *parent_inode = EXT2_ROOT_INODE;
        } else {
            char prefix[PATH_MAX_LEN];
            struct PathRef ref;
            if (prefix_len >= PATH_MAX_LEN) return -1;
            memcpy(prefix, path, prefix_len);
            prefix[prefix_len] = '\0';
            if (resolve_entry(prefix, &ref) != 0) return -1;
            if (ref.type != EXT2_FT_DIR) return -1;
            *parent_inode = ref.inode;
        }
    }

    memcpy(name, base, base_len);
    name[base_len] = '\0';
    *name_len = (uint8_t)base_len;
    return 0;
}

static int change_directory(const char *path) {
    char new_path[PATH_MAX_LEN];
    uint32_t current;
    const char *cursor;

    if (path == NULL || path[0] == '\0') return -1;

    if (path[0] == '/') {
        s_copy(new_path, "/", sizeof(new_path));
        current = EXT2_ROOT_INODE;
        cursor = path;
    } else {
        s_copy(new_path, cwd_path, sizeof(new_path));
        current = cwd_inode;
        cursor = path;
    }

    while (*cursor == '/') cursor++;
    if (*cursor == '\0') {
        cwd_inode = EXT2_ROOT_INODE;
        s_copy(cwd_path, "/", sizeof(cwd_path));
        return 0;
    }

    while (*cursor != '\0') {
        const char *start = cursor;
        uint32_t len;

        while (*cursor != '\0' && *cursor != '/') cursor++;
        len = (uint32_t)(cursor - start);
        while (*cursor == '/') cursor++;

        if (len == 0) continue;

        if (len == 1 && start[0] == '.') continue;

        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (current != EXT2_ROOT_INODE) {
                uint32_t parent;
                if (!lookup_child(current, "..", 2, &parent, NULL)) return -1;
                current = parent;
                path_pop(new_path);
            }
            continue;
        }

        {
            uint32_t inode;
            uint8_t type;
            if (len > NAME_MAX_LEN) return -1;
            if (!lookup_child(current, start, (uint8_t)len, &inode, &type)) return -1;
            if (type != EXT2_FT_DIR) return -1;
            if (!path_push(new_path, start, len)) return -1;
            current = inode;
        }
    }

    cwd_inode = current;
    s_copy(cwd_path, new_path, sizeof(cwd_path));
    return 0;
}

/* ---------- file helpers ---------- */

static uint32_t buffer_data_len(const uint8_t *buf, uint32_t cap) {
    uint32_t len = 0;
    while (len < cap && buf[len] != 0) len++;
    return len;
}

static int8_t read_file_entry(uint32_t parent, const char *name, uint8_t name_len,
                              uint32_t *out_len) {
    int8_t rc;
    memset(file_buffer, 0, FILE_BUFFER_SIZE);
    rc = fs_read(parent, name, name_len, file_buffer, FILE_BUFFER_SIZE);
    if (rc != 0) {
        *out_len = 0;
        return rc;
    }
    *out_len = buffer_data_len(file_buffer, FILE_BUFFER_SIZE);
    return 0;
}

static bool contains(const char *text, uint32_t text_len,
                     const char *pattern, uint32_t pattern_len) {
    uint32_t i;
    if (pattern_len == 0) return true;
    if (pattern_len > text_len) return false;
    for (i = 0; i + pattern_len <= text_len; i++) {
        if (s_eq_n(text + i, pattern, pattern_len)) return true;
    }
    return false;
}

static bool join_path(char *dst, const char *base, const char *name, uint32_t name_len) {
    s_copy(dst, base, PATH_MAX_LEN);
    return path_push(dst, name, name_len);
}

static bool is_descendant_dir(uint32_t ancestor_inode, uint32_t inode) {
    uint32_t depth;
    for (depth = 0; depth < 32; depth++) {
        uint32_t parent;
        if (inode == ancestor_inode) return true;
        if (inode == EXT2_ROOT_INODE) return false;
        if (!lookup_child(inode, "..", 2, &parent, NULL)) return false;
        if (parent == inode) return false;
        inode = parent;
    }
    return false;
}

/* ---------- error messages ---------- */

static void out_usage(struct Output *out, const char *usage) {
    out_str(out, "usage: ");
    out_str(out, usage);
    out_char(out, '\n');
}

static void out_fs_error(struct Output *out, const char *command, int8_t code) {
    out_str(out, command);
    out_str(out, ": ");
    if (code == -1) {
        out_str(out, "I/O error");
    } else {
        out_str(out, "error code ");
        out_u32(out, (uint32_t)(uint8_t)code);
    }
    out_char(out, '\n');
}

static void out_read_error(struct Output *out, const char *command, int8_t code) {
    if (code == 2) {
        out_str(out, command);
        out_str(out, ": file is larger than the shell buffer\n");
        return;
    }
    out_fs_error(out, command, code);
}

/* ---------- perintah ---------- */

static void cmd_cd(int argc, char **argv, struct Output *out) {
    const char *target = (argc == 2) ? argv[1] : "/";
    if (argc > 2) {
        out_usage(out, "cd [path]");
        return;
    }
    if (change_directory(target) != 0) {
        out_str(out, "cd: directory not found\n");
    }
}

static void cmd_ls(int argc, char **argv, struct Output *out) {
    uint32_t dir_inode = cwd_inode;
    uint32_t offset = 0;
    struct EntryInfo entry;

    if (argc > 2) {
        out_usage(out, "ls [path]");
        return;
    }

    if (argc == 2) {
        struct PathRef ref;
        if (resolve_entry(argv[1], &ref) != 0) {
            out_str(out, "ls: no such file or directory\n");
            return;
        }
        if (ref.type != EXT2_FT_DIR) {
            out_str(out, "ls: not a directory\n");
            return;
        }
        dir_inode = ref.inode;
    }

    if (fs_read_dir(dir_inode) != 0) {
        out_str(out, "ls: cannot read directory\n");
        return;
    }

    while (dir_next(&offset, &entry)) {
        if (is_dot_entry(&entry)) continue;
        out_str_n(out, entry.name, entry.name_len);
        if (entry.type == EXT2_FT_DIR) out_char(out, '/');
        out_char(out, '\n');
    }
}

static void cmd_mkdir(int argc, char **argv, struct Output *out) {
    uint32_t parent;
    uint8_t name_len;
    char name[NAME_MAX_LEN + 1];
    int8_t rc;

    if (argc != 2) {
        out_usage(out, "mkdir <name>");
        return;
    }
    if (resolve_parent(argv[1], &parent, name, &name_len) != 0) {
        out_str(out, "mkdir: invalid path\n");
        return;
    }

    rc = fs_write(parent, name, name_len, NULL, 0, true);
    if (rc == 1) {
        out_str(out, "mkdir: entry already exists\n");
    } else if (rc != 0) {
        out_fs_error(out, "mkdir", rc);
    }
}

static void cmd_cat(int argc, char **argv, struct Output *out) {
    struct PathRef ref;
    uint32_t file_len;
    int8_t rc;

    if (argc != 2) {
        out_usage(out, "cat <file>");
        return;
    }
    if (resolve_entry(argv[1], &ref) != 0) {
        out_str(out, "cat: no such file\n");
        return;
    }
    if (ref.type != EXT2_FT_REG_FILE) {
        out_str(out, "cat: not a file\n");
        return;
    }

    rc = read_file_entry(ref.parent_inode, ref.name, ref.name_len, &file_len);
    if (rc != 0) {
        out_read_error(out, "cat", rc);
        return;
    }
    out_str_n(out, (const char *)file_buffer, file_len);
}

static void cmd_cp(int argc, char **argv, struct Output *out) {
    struct PathRef src;
    uint32_t dst_parent;
    uint8_t dst_len;
    char dst_name[NAME_MAX_LEN + 1];
    uint32_t file_len;
    int8_t rc;

    if (argc != 3) {
        out_usage(out, "cp <source> <destination>");
        return;
    }
    if (resolve_entry(argv[1], &src) != 0) {
        out_str(out, "cp: source not found\n");
        return;
    }
    if (src.type == EXT2_FT_DIR) {
        out_str(out, "cp: copying directories is a bonus feature\n");
        return;
    }
    if (resolve_parent(argv[2], &dst_parent, dst_name, &dst_len) != 0) {
        out_str(out, "cp: invalid destination\n");
        return;
    }

    rc = read_file_entry(src.parent_inode, src.name, src.name_len, &file_len);
    if (rc != 0) {
        out_read_error(out, "cp", rc);
        return;
    }

    rc = fs_write(dst_parent, dst_name, dst_len, file_buffer, file_len, false);
    if (rc == 1) {
        out_str(out, "cp: destination already exists\n");
    } else if (rc != 0) {
        out_fs_error(out, "cp", rc);
    }
}

static void cmd_rm(int argc, char **argv, struct Output *out) {
    struct PathRef ref;
    int8_t rc;

    if (argc != 2) {
        out_usage(out, "rm <file>");
        return;
    }
    if (resolve_entry(argv[1], &ref) != 0) {
        out_str(out, "rm: no such file\n");
        return;
    }
    if (ref.type == EXT2_FT_DIR) {
        out_str(out, "rm: removing directories is a bonus feature\n");
        return;
    }
    if (ref.name_len == 0) {
        out_str(out, "rm: invalid entry\n");
        return;
    }

    rc = fs_delete(ref.parent_inode, ref.name, ref.name_len, false);
    if (rc != 0) out_fs_error(out, "rm", rc);
}

static int move_directory(uint32_t src_parent, const char *src_name, uint8_t src_len,
                          uint32_t dst_parent, const char *dst_name, uint8_t dst_len,
                          uint32_t depth) {
    uint32_t src_inode;
    uint32_t dst_inode;
    struct EntryInfo child;
    int8_t rc;

    if (depth >= MOVE_DEPTH_MAX) return -2;

    rc = fs_write(dst_parent, dst_name, dst_len, NULL, 0, true);
    if (rc != 0) return rc;

    if (!lookup_child(src_parent, src_name, src_len, &src_inode, NULL)) return -3;
    if (!lookup_child(dst_parent, dst_name, dst_len, &dst_inode, NULL)) return -3;

    while (get_nth_entry(src_inode, 0, &child)) {
        if (child.type == EXT2_FT_DIR) {
            rc = (int8_t)move_directory(src_inode, child.name, child.name_len,
                                        dst_inode, child.name, child.name_len, depth + 1);
            if (rc != 0) return rc;
        } else {
            uint32_t file_len;
            rc = read_file_entry(src_inode, child.name, child.name_len, &file_len);
            if (rc != 0) return rc;
            rc = fs_write(dst_inode, child.name, child.name_len, file_buffer, file_len, false);
            if (rc != 0) return rc;
            rc = fs_delete(src_inode, child.name, child.name_len, false);
            if (rc != 0) return rc;
        }
    }

    return fs_delete(src_parent, src_name, src_len, true);
}

static void cmd_mv(int argc, char **argv, struct Output *out) {
    struct PathRef src;
    uint32_t dst_parent;
    uint8_t dst_len;
    char dst_name[NAME_MAX_LEN + 1];
    int8_t rc;

    if (argc != 3) {
        out_usage(out, "mv <source> <destination>");
        return;
    }
    if (resolve_entry(argv[1], &src) != 0) {
        out_str(out, "mv: source not found\n");
        return;
    }
    if (resolve_parent(argv[2], &dst_parent, dst_name, &dst_len) != 0) {
        out_str(out, "mv: invalid destination\n");
        return;
    }

    if (src.type == EXT2_FT_DIR) {
        bool is_dot = (src.name_len == 1 && src.name[0] == '.')
                   || (src.name_len == 2 && src.name[0] == '.' && src.name[1] == '.');
        if (src.name_len == 0 || is_dot) {
            out_str(out, "mv: invalid source\n");
            return;
        }
        if (is_descendant_dir(src.inode, dst_parent)) {
            out_str(out, "mv: cannot move a directory into itself\n");
            return;
        }
        rc = (int8_t)move_directory(src.parent_inode, src.name, src.name_len,
                                    dst_parent, dst_name, dst_len, 0);
        if (rc != 0) {
            if (rc == 1) out_str(out, "mv: destination already exists\n");
            else if (rc == -2) out_str(out, "mv: directory nesting too deep\n");
            else out_fs_error(out, "mv", rc);
        }
        return;
    }

    {
        uint32_t file_len;
        rc = read_file_entry(src.parent_inode, src.name, src.name_len, &file_len);
        if (rc != 0) {
            out_fs_error(out, "mv", rc);
            return;
        }
        rc = fs_write(dst_parent, dst_name, dst_len, file_buffer, file_len, false);
        if (rc == 1) {
            out_str(out, "mv: destination already exists\n");
            return;
        }
        if (rc != 0) {
            out_fs_error(out, "mv", rc);
            return;
        }
        rc = fs_delete(src.parent_inode, src.name, src.name_len, false);
        if (rc != 0) {
            out_str(out, "mv: copied but failed to remove the source\n");
        }
    }
}

static void cmd_find(int argc, char **argv, struct Output *out) {
    uint32_t pattern_len;
    uint32_t head;
    uint32_t tail;
    bool skipped = false;

    if (argc != 2) {
        out_usage(out, "find <name>");
        return;
    }
    pattern_len = s_len(argv[1]);
    if (pattern_len == 0 || pattern_len > NAME_MAX_LEN) {
        out_str(out, "find: invalid name\n");
        return;
    }

    find_queue[0].inode = EXT2_ROOT_INODE;
    s_copy(find_queue[0].path, "/", sizeof(find_queue[0].path));
    head = 0;
    tail = 1;

    while (head < tail) {
        uint32_t dir_inode = find_queue[head].inode;
        char base[PATH_MAX_LEN];
        uint32_t offset = 0;
        struct EntryInfo entry;

        s_copy(base, find_queue[head].path, sizeof(base));
        head++;

        if (fs_read_dir(dir_inode) != 0) continue;

        while (dir_next(&offset, &entry)) {
            char child_path[PATH_MAX_LEN];

            if (is_dot_entry(&entry)) continue;

            if (!join_path(child_path, base, entry.name, entry.name_len)) {
                skipped = true;
                continue;
            }

            if (entry.name_len == pattern_len
                && s_eq_n(entry.name, argv[1], pattern_len)) {
                out_str(out, child_path);
                out_char(out, '\n');
            }

            if (entry.type == EXT2_FT_DIR) {
                if (tail < FIND_QUEUE_MAX) {
                    find_queue[tail].inode = entry.inode;
                    s_copy(find_queue[tail].path, child_path,
                           sizeof(find_queue[tail].path));
                    tail++;
                } else {
                    skipped = true;
                }
            }
        }
    }

    if (skipped) {
        out_str(out, "find: some entries were skipped (path/queue limit)\n");
    }
}

static void cmd_grep(int argc, char **argv, const char *input, uint32_t input_len,
                     struct Output *out) {
    const char *text;
    uint32_t text_len;
    uint32_t pattern_len;
    uint32_t i;

    if (argc < 2 || argc > 3) {
        out_usage(out, "grep <pattern> [file]");
        return;
    }

    if (argc == 3) {
        struct PathRef ref;
        uint32_t file_len;
        int8_t rc;

        if (resolve_entry(argv[2], &ref) != 0) {
            out_str(out, "grep: no such file\n");
            return;
        }
        if (ref.type != EXT2_FT_REG_FILE) {
            out_str(out, "grep: not a file\n");
            return;
        }
        rc = read_file_entry(ref.parent_inode, ref.name, ref.name_len, &file_len);
        if (rc != 0) {
            out_read_error(out, "grep", rc);
            return;
        }
        text = (const char *)file_buffer;
        text_len = file_len;
    } else {
        text = input;
        text_len = input_len;
    }

    pattern_len = s_len(argv[1]);
    i = 0;
    while (i < text_len) {
        uint32_t start = i;
        uint32_t end;

        while (i < text_len && text[i] != '\n') i++;
        end = i;
        if (i < text_len && text[i] == '\n') i++;

        if (contains(text + start, end - start, argv[1], pattern_len)) {
            out_str_n(out, text + start, end - start);
            out_char(out, '\n');
        }
    }
}

static void cmd_help(struct Output *out) {
    out_str(out, "built-in : cd ls mkdir cat cp rm mv find grep help\n");
    out_str(out, "pipeline : command1 | command2 | ...\n");
}

static void execute(int argc, char **argv, const char *input, uint32_t input_len,
                    struct Output *out) {
    if (argc <= 0) return;

    if (s_cmp(argv[0], "cd") == 0) cmd_cd(argc, argv, out);
    else if (s_cmp(argv[0], "ls") == 0) cmd_ls(argc, argv, out);
    else if (s_cmp(argv[0], "mkdir") == 0) cmd_mkdir(argc, argv, out);
    else if (s_cmp(argv[0], "cat") == 0) cmd_cat(argc, argv, out);
    else if (s_cmp(argv[0], "cp") == 0) cmd_cp(argc, argv, out);
    else if (s_cmp(argv[0], "rm") == 0) cmd_rm(argc, argv, out);
    else if (s_cmp(argv[0], "mv") == 0) cmd_mv(argc, argv, out);
    else if (s_cmp(argv[0], "find") == 0) cmd_find(argc, argv, out);
    else if (s_cmp(argv[0], "grep") == 0) cmd_grep(argc, argv, input, input_len, out);
    else if (s_cmp(argv[0], "help") == 0) cmd_help(out);
    else {
        out_str(out, argv[0]);
        out_str(out, ": command not found\n");
    }
}

/* ---------- pipeline ---------- */

static uint32_t split_stages(char *line, char **stages, uint32_t max) {
    uint32_t count = 1;
    char *cursor;

    stages[0] = line;
    for (cursor = line; *cursor != '\0'; cursor++) {
        if (*cursor == '|') {
            *cursor = '\0';
            if (count < max) stages[count] = cursor + 1;
            count++;
        }
    }
    return count;
}

static int parse_argv(char *stage, char **argv, int max) {
    char *cursor = stage;
    int argc = 0;

    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (*cursor == '\0') break;
        if (argc >= max) break;
        argv[argc++] = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') cursor++;
        if (*cursor != '\0') *cursor++ = '\0';
    }
    return argc;
}

static void run_pipeline(char *line) {
    char *stages[STAGE_MAX];
    uint32_t stage_count = split_stages(line, stages, STAGE_MAX);
    const char *input = NULL;
    uint32_t input_len = 0;
    uint32_t active = 0;
    uint32_t i;
    bool truncated = false;

    if (stage_count > STAGE_MAX) {
        emit("shell: too many pipeline stages\n", 32, COLOR_ERROR);
        return;
    }

    for (i = 0; i < stage_count; i++) {
        char *argv[ARG_MAX];
        int argc = parse_argv(stages[i], argv, ARG_MAX);
        struct Output out;

        if (argc == 0) continue;

        out_init(&out, io_buffer[active], IO_BUFFER_SIZE);
        execute(argc, argv, input, input_len, &out);
        if (out.truncated) truncated = true;

        input = out.buf;
        input_len = out.len;
        active ^= 1;
    }

    if (input != NULL && input_len > 0) emit(input, input_len, COLOR_DEFAULT);
    if (truncated) emit("shell: output truncated\n", 24, COLOR_ERROR);
}

/* ---------- input ---------- */

static char get_char(void) {
    union {
        char     bytes[4];
        uint32_t word;
    } value;
    int32_t result;

    value.word = 0;
    result = syscall(SYS_GETCHAR, (uint32_t)&value, 0, 0);

    if (value.bytes[0] != 0) return value.bytes[0];
    if (result > 0 && result < 256) return (char)result;
    return 0;
}

static void echo_char(char c) {
    syscall(SYS_PUTCHAR, (uint32_t)(unsigned char)c, COLOR_DEFAULT, 0);
}

static uint32_t read_line(char *buffer, uint32_t capacity) {
    uint32_t len = 0;

    for (;;) {
        char c = get_char();
        if (c == '\0') continue;
        if (c == '\r') c = '\n';

        if (c == '\n') {
            echo_char('\n');
            break;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                echo_char('\b');
                echo_char(' ');
                echo_char('\b');
            }
            continue;
        }
        if (c == '\t') c = ' ';
        if ((unsigned char)c < 32 || (unsigned char)c > 126) continue;
        if (len + 1 < capacity) {
            buffer[len++] = c;
            echo_char(c);
        }
    }

    buffer[len] = '\0';
    return len;
}

int main(void) {
    memset(io_buffer, 0, sizeof(io_buffer));
    memset(line_buffer, 0, sizeof(line_buffer));
    cwd_inode = EXT2_ROOT_INODE;
    s_copy(cwd_path, "/", sizeof(cwd_path));

    syscall(SYS_KEYBOARD_ACTIVATE, 0, 0, 0);

    emit("konOSuba shell\n", 15, COLOR_DEFAULT);
    emit("ketik 'help' untuk daftar perintah\n", 35, COLOR_DEFAULT);

    for (;;) {
        uint32_t len;

        emit(cwd_path, s_len(cwd_path), COLOR_PATH);
        emit(" $ ", 3, COLOR_DEFAULT);

        len = read_line(line_buffer, LINE_MAX);
        if (len == 0) continue;

        run_pipeline(line_buffer);
    }

    return 0;
}
