# konOSuba

> **konOSuba** adalah sistem operasi x86 32-bit Protected Mode untuk Tugas Besar IF2130 Sistem Operasi 2026.
> Kernel ditulis dari nol dalam C dan NASM, lalu dijalankan di QEMU.

<img src="mascot.png" width="720" alt="Maskot konOSuba: Darkness, Kazuma, Aqua, dan Megumin">

Credit: karakter dari *KonoSuba: God's Blessing on This Wonderful World!*
(Natsume Akatsuki / Kurone Mishima).

## Daftar Isi

- [Informasi Umum](#informasi-umum)
- [Anggota Kelompok](#anggota-kelompok)
- [Fitur](#fitur)
- [Cara Run](#cara-run)
- [Debug](#debug)
- [Arsitektur](#arsitektur)
  - [Urutan boot](#urutan-boot)
  - [Peta memori](#peta-memori)
  - [Memory manager](#memory-manager)
  - [Disk dan file system](#disk-dan-file-system)
  - [Program user dan syscall](#program-user-dan-syscall)
- [Struktur Repository](#struktur-repository)
- [Referensi](#referensi)

## Informasi Umum

| Item | Keterangan |
|------|------------|
| Nama sistem operasi | **konOSuba** |
| Arsitektur | x86 32-bit Protected Mode |
| Bootloader | GRUB Legacy (Multiboot 1) |
| Kernel | C dan NASM; di-load fisik di `0x100000`, di-link di higher half `0xC0100000` |
| File system | EXT2 edisi IF2130 pada disk 4 MiB |
| Antarmuka | Shell user mode: REPL, pipeline, perintah bawaan, dan aplikasi eksternal |
| Emulator | QEMU (`qemu-system-i386`) |
| Status | Implementasi Bab 1-3 tersedia; integrasi user mode, syscall, dan scheduler Bab 4 sedang dikerjakan |

## Anggota Kelompok

| Nama | NIM | Bagian utama |
| :--- | :-- | :----------- |

## Fitur

Fitur bertanda `[x]` sudah berjalan pada build saat ini, sedangkan `[ ]` masih dalam pengerjaan.

**CPU dan memori**

- [x] GDT: kernel code dan data (flat 4 GiB), user code dan data, serta deskriptor TSS.
- [x] Paging 2-level dengan page 4 KiB: higher half kernel, recursive page table,
  dan memory manager berbasis frame allocator.
- [x] Peta memori 128 MiB dengan 31.744 frame user yang dapat dialokasikan.

**Interrupt dan driver**

- [x] PIC remap, IDT, ISR stub, dan handler keyboard.
- [x] Driver framebuffer teks VGA 80x25.
- [x] Driver keyboard PS/2 (scancode set 1).
- [x] Driver disk ATA PIO (LBA28).

**File system**

- [x] EXT2 edisi IF2130: inisialisasi, read, read directory, write, dan delete.
- [x] Struktur disk 8 block group, 64 inode per grup, serta alokasi block langsung dan indirect.

**Program user dan shell**

- [ ] User mode dan system call. Rangka `crt0.s`, `user-linker.ld`, TSS, dan
  `kernel_execute_user_program` sudah tersedia; integrasi ke kernel masih berjalan.
- [ ] Shell dan aplikasi user. Source `src/user-shell.c`, `src/user-hello-world.c`,
  `src/user-counter.c`, dan `src/user-clock.c` sudah tersedia dan menunggu user mode serta syscall.
- [ ] Process, scheduler, dan multitasking.

## Cara Run

**Prasyarat (Ubuntu/Debian di WSL)**

```bash
sudo apt update
sudo apt install build-essential nasm genisoimage qemu-system-x86 qemu-utils
```

**Build dan jalankan**

```bash
make build   # build bin/OS2025.iso
make run     # build, lalu QEMU menunggu debugger (flag -s -S); lihat bagian Debug
```

**Target Makefile**

| Target | Hasil |
|--------|-------|
| `make build` | Kernel dan `bin/OS2025.iso` |
| `make kernel` | `bin/kernel` saja |
| `make iso` | ISO bootable `bin/OS2025.iso` |
| `make run` | Build lengkap lalu QEMU dengan gdb server aktif |
| `make user-shell` | `bin/shell` (flat binary) dan `bin/shell_elf` (ELF32 untuk gdb) |
| `make user-apps` | `bin/hello-world`, `bin/counter`, `bin/clock` beserta versi `*_elf` |
| `make disk` | Menimpa `bin/storage.bin` dengan disk kosong 4 MiB |
| `make clean` | Menghapus kernel, ISO, dan object file (disk tidak ikut terhapus) |

Disk image `bin/storage.bin` (4 MiB) dibuat otomatis saat `make run` jika belum ada dan **tidak** dibuat
ulang pada run berikutnya, sehingga isi file system tetap tersimpan antar sesi.

Binary program user yang dihasilkan Makefile masih berupa file di `bin/`. Agar dapat dibaca kernel,
file tersebut perlu dimasukkan ke `storage.bin` melalui Inserter dari kit dengan `EXT2DriverRequest`
yang menunjuk parent inode direktori tujuan.

**Catatan WSL**

- Build lebih cepat jika repository diletakkan di filesystem Linux (`~/...`), bukan di `/mnt/c/...`.
- Jendela QEMU membutuhkan WSLg (Windows 11) atau X server. Jika muncul error `gtk initialization failed`,
  pastikan `echo $DISPLAY` tidak kosong.

## Debug

`make run` memakai `-s -S`: QEMU membuka gdb server di `localhost:1234` dan berhenti sampai debugger
menyambung.

```bash
gdb bin/kernel
(gdb) target remote :1234
(gdb) break kernel_setup
(gdb) continue
```

Simbol program user dapat ditambahkan agar debugging di ring 3 tetap terbaca:

```gdb
add-symbol-file bin/shell_elf
add-symbol-file bin/hello-world_elf
add-symbol-file bin/clock_elf
```

Setelah paging aktif, semua alamat yang terlihat di debugger adalah virtual address (`0xC0xxxxxx`).
Sebelum debugger menyambung, layar QEMU tampil hitam dengan pesan "Guest has not initialized the display".
Itu normal.

## Arsitektur

### Urutan boot

GRUB Legacy memuat kernel ke physical `0x100000`, lalu `kernel-entrypoint.s`:

1. Mengisi page table kernel (1024 entry x 4 KiB = 4 MiB pertama).
2. Memasang page table itu di PDE 0 (identity mapping sementara) dan PDE 768 (`0xC0000000`),
   serta PDE 1023 yang menunjuk page directory itu sendiri (recursive mapping).
3. Mengisi CR3, menyalakan paging, lalu melompat ke higher half (`0xC0100000`).
4. Menghapus identity mapping, menyiapkan stack, dan memanggil `kernel_setup()`.

### Peta memori

| Region | Virtual | Physical | Keterangan |
|--------|---------|----------|------------|
| Kernel | `0xC0000000` - `0xC03FFFFF` | `0x000000` - `0x3FFFFF` | Higher half, page 4 KiB. Image di-link di `0xC0100000` |
| Framebuffer | `0xC00B8000` | `0xB8000` | Teks VGA 80x25 |
| Frame user | `0x00000000` - `0xBFFFFFFF` | `0x00400000` - `0x07FFFFFF` | Dikelola memory manager (31.744 frame x 4 KiB, RAM 128 MiB) |
| Page table window | `0xFFC00000` - `0xFFFFFFFF` | - | Recursive mapping (PDE 1023) |

### Memory manager

Implementasi berada di `src/memory/paging.c`:

- `paging_allocate_user_page_frame()`: First Fit pada `page_frame_map`. Menolak address kernel
  (`>= 0xC0000000`) dan address yang sudah terpetakan. Page table dibuat otomatis lewat recursive
  mapping jika belum ada.
- `paging_free_user_page_frame()`: Melepas mapping dan mengembalikan frame ke pool.
- `paging_allocate_check()`: Memeriksa apakah sisa frame cukup untuk jumlah byte yang diminta.

### Disk dan file system

Storage 4 MiB (`bin/storage.bin`), block 512 byte, LBA zero-indexed.

| Block | Isi |
|-------|-----|
| 0 | Boot sector |
| 1 | Superblock |
| 2 | Block Group Descriptor table (8 grup) |
| 3 / 4 / 5-20 | Block bitmap / inode bitmap / inode table grup 0 |
| 21 ... | Data grup 0 (directory root, inode 1) |
| `g*1024` ... | Grup `g` (1..7): block bitmap, inode bitmap, 16 block inode table, lalu data |

- 8 grup x 1024 block, 64 inode per grup (inode 128 byte, 4 inode per block).
- Inode memakai 12 direct block, 1 indirect, dan 1 doubly indirect.
- Directory memakai format entry ext2 standar (`inode`, `rec_len`, `name_len`, `file_type`, nama).

Mengecek disk dengan hex editor: offset byte = `LBA * 0x200`. Contoh: block 21 (root directory)
ada di `0x2A00`.

### Program user dan syscall

Program user di-link dengan `src/user-linker.ld` pada alamat `0x0`, entry point `_start` dari `crt0.s`.
Stack user berada di bawah `0xC0000000` dan program berjalan di ring 3. Komunikasi dengan kernel
memakai inter-privilege interrupt `int $0x30` dengan `eax` sebagai nomor layanan dan `ebx`, `ecx`,
`edx` sebagai argumen.

| eax | Layanan | Argumen |
|-----|---------|---------|
| 0 | `read` | `ebx` request, `ecx` pointer retcode |
| 1 | `read_directory` | `ebx` request, `ecx` pointer retcode |
| 2 | `write` | `ebx` request, `ecx` pointer retcode |
| 3 | `delete` | `ebx` request, `ecx` pointer retcode |
| 4 | `getchar` | `ebx` pointer karakter |
| 5 | `putchar` | `ebx` karakter, `ecx` warna |
| 6 | `puts` | `ebx` buffer, `ecx` jumlah karakter, `edx` warna |
| 7 | `keyboard_activate` | - |
| 10 | `exit` | `ebx` exit code |
| 11 | `exec` | `ebx` parent inode, `ecx` nama file, `edx` panjang nama |
| 12 | `ps` | `ebx` buffer `ProcessInfo`, `ecx` kapasitas |
| 13 | `kill` | `ebx` PID |
| 14 | `get_time` | `ebx` buffer `TimeInfo` |
| 15 | `move_cursor` | `ebx` row, `ecx` col |
| 16 | `get_cursor` | `ebx` pointer row, `ecx` pointer col |

Layanan 0-7 mengikuti tabel buku Chapter 3. Layanan 10-16 disiapkan untuk Chapter 4 dan kontraknya
didefinisikan di `src/header/syscall.h`.

Shell menyediakan REPL dengan current working directory, output buffer untuk pipeline, serta perintah
`cd`, `ls`, `mkdir`, `cat`, `cp`, `rm`, `mv`, `find`, `grep`, `exec`, `ps`, `kill`, `export`, dan `help`.
Aplikasi eksternal yang disiapkan adalah `hello-world`, `counter`, dan `clock`.

PATH bersifat dinamis dan persisten: `export PATH=...` atau `export PATH+=...` mengubah daftar direktori,
menyimpannya sebagai file `/path` di root, dan memuatnya kembali saat shell dijalankan. Nama perintah
tanpa slash dicari pada setiap direktori PATH, lalu pada current working directory.

## Struktur Repository

```text
konOSuba/
  Makefile
  README.md
  mascot.png
  other/grub1             stage2_eltorito GRUB Legacy
  bin/                    hasil build (kernel, ISO, program user)
  src/
    kernel-entrypoint.s   entry point, paging awal, load_gdt, eksekusi program user
    linker.ld             kernel di-link di 0xC0100000, di-load di 0x100000
    menu.lst              konfigurasi GRUB Legacy
    kernel.c              kernel_setup()
    crt0.s                startup routine program user
    user-linker.ld        layout program user di 0x0
    user-shell.c          shell user mode
    user-hello.c          tes program user Bab 2/3
    user-hello-world.c    aplikasi eksternal "Hello World!"
    user-counter.c        aplikasi eksternal pencacah
    user-clock.c          program clock
    cpu/                  gdt, idt, interrupt, portio
    driver/               framebuffer, keyboard, disk
    filesystem/           ext2
    memory/               paging dan memory manager
    stdlib/               string
    header/               header semua modul, termasuk syscall.h
```

## Referensi

- Buku panduan IF2130 - 2026
- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A
  (Ch. 4 Paging, Ch. 6 Interrupt)
- OSDev Wiki: Higher Half Kernel, Fractal Page Mapping, Page Frame Allocation, ATA PIO Mode
