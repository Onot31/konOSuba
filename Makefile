# Variables
CC = gcc
ASM = nasm
LIN = ld
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME = OS2025
DISK_NAME = storage

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386

# Targets
run: all $(OUTPUT_FOLDER)/$(DISK_NAME).bin insert-shell insert-apps
	@powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$(wslpath -w $(CURDIR)/tools/snap-qemu.ps1)" >/dev/null 2>&1 & \
	qemu-system-i386 -name konOSuba -display gtk -rtc base=localtime -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

run-debug: all $(OUTPUT_FOLDER)/$(DISK_NAME).bin insert-shell insert-apps
	@powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$(wslpath -w $(CURDIR)/tools/snap-qemu.ps1)" >/dev/null 2>&1 & \
	qemu-system-i386 -name konOSuba -display gtk -s -S -rtc base=localtime -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

all: build
build: iso

clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/kernel $(OUTPUT_FOLDER)/iso

kernel:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/cmos.c -o $(OUTPUT_FOLDER)/cmos.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/filesystem/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/process/context-switch.s -o $(OUTPUT_FOLDER)/context-switch.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/process/process.c -o $(OUTPUT_FOLDER)/process.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/process/scheduler.c -o $(OUTPUT_FOLDER)/scheduler.o
	@$(LIN) $(LFLAGS) $(OUTPUT_FOLDER)/*.o -o $(OUTPUT_FOLDER)/kernel
	@rm -f $(OUTPUT_FOLDER)/*.o

iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os -input-charset utf8 -quiet -boot-info-table -o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -rf $(OUTPUT_FOLDER)/iso/

user-shell:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/user-shell.c -o user-shell.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o string.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary crt0.o user-shell.o string.o -o $(OUTPUT_FOLDER)/shell
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=elf32-i386 crt0.o user-shell.o string.o -o $(OUTPUT_FOLDER)/shell_elf
	@rm -f *.o

USER_APP_NAMES = hello-world counter clock
USER_APP_BINS  = $(addprefix $(OUTPUT_FOLDER)/,$(USER_APP_NAMES))

user-apps: $(USER_APP_BINS)

$(USER_APP_BINS): $(OUTPUT_FOLDER)/%: $(SOURCE_FOLDER)/user-%.c $(SOURCE_FOLDER)/crt0.s $(SOURCE_FOLDER)/user-linker.ld
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o $(OUTPUT_FOLDER)/crt0-$*.o
	@$(CC) $(CFLAGS) -fno-pie $< -o $(OUTPUT_FOLDER)/$*.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary $(OUTPUT_FOLDER)/crt0-$*.o $(OUTPUT_FOLDER)/$*.o -o $@
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=elf32-i386 $(OUTPUT_FOLDER)/crt0-$*.o $(OUTPUT_FOLDER)/$*.o -o $@_elf
	@rm -f $(OUTPUT_FOLDER)/crt0-$*.o $(OUTPUT_FOLDER)/$*.o

disk:
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M

# Disk dibuat hanya jika belum ada, sehingga isi file system tetap tersimpan antar run.
# Gunakan "make disk" untuk mengosongkan disk.
$(OUTPUT_FOLDER)/$(DISK_NAME).bin:
	@mkdir -p $(OUTPUT_FOLDER)
	@qemu-img create -f raw $@ 4M

# Host tool untuk memasukkan file ke storage.bin
inserter:
	@$(CC) -Wno-builtin-declaration-mismatch -g -I$(SOURCE_FOLDER) $(SOURCE_FOLDER)/filesystem/ext2.c $(SOURCE_FOLDER)/external-inserter.c -o $(OUTPUT_FOLDER)/inserter

insert-shell: inserter user-shell $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); bin_inode=$$(./inserter -d .bin 1 $(DISK_NAME).bin | sed -n 's/^Inode: //p'); \
	./inserter -f shell $$bin_inode $(DISK_NAME).bin >/dev/null

insert-apps: inserter user-apps $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@cd $(OUTPUT_FOLDER); bin_inode=$$(./inserter -d .bin 1 $(DISK_NAME).bin | sed -n 's/^Inode: //p'); \
	./inserter -f hello-world $$bin_inode $(DISK_NAME).bin >/dev/null; \
	./inserter -f counter $$bin_inode $(DISK_NAME).bin >/dev/null; \
	./inserter -f clock $$bin_inode $(DISK_NAME).bin >/dev/null
