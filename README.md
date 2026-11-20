# konOSuba

> Sistem operasi x86 32-bit Protected Mode untuk Tugas Besar IF2130 Sistem Operasi (2026).
> Ditulis dari nol dalam C dan NASM, dijalankan di QEMU.

<!-- TODO: isi nama kelompok, nama anggota, dan NIM -->

**Nama kelompok:** NAMA_KELOMPOK

| Nama | NIM | Bagian utama |
|------|-----|--------------|
| NAMA_1 | 135xxxxx | ... |
| NAMA_2 | 135xxxxx | ... |
| NAMA_3 | 135xxxxx | ... |
| NAMA_4 | 135xxxxx | ... |

## Daftar Isi

- [Maskot](#maskot)
- [Fitur](#fitur)
- [Cara Run](#cara-run)
- [Debug](#debug)
- [Arsitektur](#arsitektur)
- [Struktur Repository](#struktur-repository)
- [Referensi](#referensi)

## Maskot

![Credit: 朧月](mascot.jpg)

## Fitur

- [x] GDT: kernel code dan data (flat 4 GiB), user code dan data, TSS
- [x] Interrupt: PIC remap, IDT, ISR stub
- [x] Driver framebuffer teks 80x25 dan keyboard (PS/2, scancode set 1)
- [x] Driver disk ATA PIO (LBA28)
- [x] File system EXT2 edisi IF2130: inisialisasi, read, read directory, write, delete
- [x] Paging 2-level (4 KiB), higher half kernel, recursive page table, memory manager (frame allocator)
- [ ] User mode, system call, shell
- [ ] Process, scheduler, multitasking

<!-- Centang sesuai kondisi release yang dikumpulkan. -->

## Cara Run

Paket yang dibutuhkan (Ubuntu/Debian di WSL):

```bash
sudo apt update
sudo apt install build-essential nasm genisoimage qemu-system-x86 qemu-utils
```

Build dan jalankan:

```bash
make build   # build bin/OS2025.iso
make run     # build, lalu QEMU menunggu debugger (flag -s -S), lihat bagian Debug
```

Disk image `bin/storage.bin` (4 MiB) dibuat otomatis saat `make run` jika belum ada, dan **tidak** dibuat ulang
pada run berikutnya, sehingga isi file system tetap tersimpan. Perintah lain:

```bash
make disk    # menimpa storage.bin dengan disk kosong
make clean   # menghapus hasil build (disk tidak ikut terhapus)
```

Catatan WSL:
- Build lebih cepat jika repository diletakkan di filesystem Linux (`~/...`), bukan di `/mnt/c/...`.
- Jendela QEMU membutuhkan WSLg (Windows 11) atau X server. Jika muncul error `gtk initialization failed`,
  pastikan `echo $DISPLAY` tidak kosong.

## Debug

`make run` memakai `-s -S`: QEMU membuka gdb server di `localhost:1234` dan berhenti sampai debugger menyambung.

```bash
gdb bin/kernel
(gdb) target remote :1234
(gdb) break kernel_setup
(gdb) continue
```

Setelah paging aktif, semua alamat yang terlihat di debugger adalah virtual address (`0xC0xxxxxx`).
Sebelum debugger menyambung, layar QEMU tampil hitam dengan pesan "Guest has not initialized the display". Itu normal.

## Arsitektur

### Urutan boot

GRUB Legacy memuat kernel ke physical `0x100000`, lalu `kernel-entrypoint.s`:

1. Mengisi page table kernel (1024 entry x 4 KiB = 4 MiB pertama).
2. Memasang page table itu di PDE 0 (identity mapping sementara) dan PDE 768 (`0xC0000000`),
   serta PDE 1023 yang menunjuk page directory itu sendiri (recursive mapping).
3. Mengisi CR3, menyalakan paging, melompat ke higher half (`0xC0100000`).
4. Menghapus identity mapping, menyiapkan stack, memanggil `kernel_setup()`.

### Peta memori

| Region | Virtual | Physical | Keterangan |
|--------|---------|----------|------------|
| Kernel | `0xC0000000` - `0xC03FFFFF` | `0x000000` - `0x3FFFFF` | Higher half, page 4 KiB. Image di-link di `0xC0100000` |
| Framebuffer | `0xC00B8000` | `0xB8000` | Teks VGA 80x25 |
| Frame user | `0x00000000` - `0xBFFFFFFF` | `0x400000` - `0x7FFFFFF` | Dikelola memory manager (31.744 frame x 4 KiB, RAM 128 MiB) |
| Page table window | `0xFFC00000` - `0xFFFFFFFF` | - | Recursive mapping (PDE 1023) |

### Memory manager (`src/memory/paging.c`)

- `paging_allocate_user_page_frame()`: First Fit pada `page_frame_map`. Menolak address kernel (`>= 0xC0000000`)
  dan address yang sudah terpetakan. Page table dibuat otomatis lewat recursive mapping jika belum ada.
- `paging_free_user_page_frame()`: melepas mapping dan mengembalikan frame ke pool.
- `paging_allocate_check()`: memeriksa apakah sisa frame cukup untuk jumlah byte yang diminta.

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

Mengecek disk dengan hex editor: offset byte = `LBA * 0x200`. Contoh: block 21 (root directory) ada di `0x2A00`.

## Struktur Repository

```
src/
  kernel-entrypoint.s   entry point, paging awal, load_gdt, TSS, eksekusi program user
  linker.ld             kernel di-link di 0xC0100000, di-load di 0x100000
  menu.lst              konfigurasi GRUB Legacy
  kernel.c              kernel_setup()
  cpu/                  gdt, idt, interrupt, portio
  driver/               framebuffer, keyboard, disk
  filesystem/           ext2
  memory/               paging dan memory manager
  stdlib/               string
  header/               header untuk semua modul di atas
  crt0.s, user-*.c, user-linker.ld   program user
other/grub1             stage2_eltorito GRUB Legacy
bin/                    hasil build (iso, kernel, storage.bin)
```

## Referensi

- Buku panduan IF2130 - 2026
- Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A (Ch. 4 Paging, Ch. 6 Interrupt)
- OSDev Wiki: Higher Half Kernel, Fractal Page Mapping, Page Frame Allocation, ATA PIO Mode
