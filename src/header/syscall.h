#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>

/*
 * Kontrak syscall bab 3 & 4.
 *
 * ABI dasar (buku 3.4.1.1): int $0x30, eax = nomor layanan,
 * ebx/ecx/edx = argumen, nilai balik di eax.
 *
 * Nomor 0-7 mengikuti tabel buku. Nomor 8 ke atas adalah layanan
 * tambahan bab 4; dispatcher kernel (bagian C) wajib memakai nomor
 * dan layout argumen yang sama dengan header ini.
 */

/* Bab 3 - filesystem, keyboard, dan text output */
#define SYSCALL_FS_READ              0  /* ebx=EXT2DriverRequest*, ecx=int8_t* retcode */
#define SYSCALL_FS_READ_DIRECTORY    1  /* ebx=EXT2DriverRequest*, ecx=int8_t* retcode */
#define SYSCALL_FS_WRITE             2  /* ebx=EXT2DriverRequest*, ecx=int8_t* retcode */
#define SYSCALL_FS_DELETE            3  /* ebx=EXT2DriverRequest*, ecx=int8_t* retcode */
#define SYSCALL_GETCHAR              4  /* ebx=char*, balik karakter di eax */
#define SYSCALL_PUTCHAR              5  /* ebx=char, ecx=warna */
#define SYSCALL_PUTS                 6  /* ebx=char*, ecx=jumlah, edx=warna */
#define SYSCALL_KEYBOARD_ACTIVATE    7

/* Bab 4 - process management */
#define SYSCALL_EXIT                10  /* ebx=exit code; terminasi proses pemanggil */
#define SYSCALL_EXEC                11  /* ebx=parent_inode, ecx=nama file, edx=name_len;
                                           balik 0 sukses, <0 gagal */
#define SYSCALL_PS                  12  /* ebx=struct ProcessInfo*, ecx=kapasitas;
                                           balik jumlah proses, <0 gagal */
#define SYSCALL_KILL                13  /* ebx=PID; balik 0 sukses, <0 gagal */

/* Bab 4 - CMOS dan posisi teks */
#define SYSCALL_GET_TIME            14  /* ebx=struct TimeInfo*; balik 0 sukses, <0 gagal */
#define SYSCALL_MOVE_CURSOR         15  /* ebx=row, ecx=col; balik 0 sukses */
#define SYSCALL_GET_CURSOR          16  /* ebx=uint8_t* row, ecx=uint8_t* col; balik 0 sukses */

#define PROCESS_INFO_NAME_LEN       32
#define PROCESS_INFO_MAX            16

/* Nilai state mengikuti enum PROCESS_STATE pada process.h milik kernel. */
#define USER_PROCESS_STATE_INACTIVE 0
#define USER_PROCESS_STATE_READY    1
#define USER_PROCESS_STATE_RUNNING  2

struct ProcessInfo {
    uint32_t pid;
    uint32_t state;
    char     name[PROCESS_INFO_NAME_LEN];  /* NUL-terminated bila < 32 karakter */
} __attribute__((packed));

struct TimeInfo {
    uint8_t hour;    /* nilai biner 0-23, bukan BCD */
    uint8_t minute;  /* nilai biner 0-59 */
    uint8_t second;  /* nilai biner 0-59 */
} __attribute__((packed));

#endif
