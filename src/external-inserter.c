#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// Global variable
uint8_t *image_storage;
uint8_t *file_buffer;
uint8_t *read_buffer;
bool is_replace = false;

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            (uint8_t*) ptr + BLOCK_SIZE*i, 
            image_storage + BLOCK_SIZE*(logical_block_address+i), 
            BLOCK_SIZE
        );
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            image_storage + BLOCK_SIZE*(logical_block_address+i), 
            (uint8_t*) ptr + BLOCK_SIZE*i, 
            BLOCK_SIZE
        );
    }
}

int main(int argc, char *argv[]) {
    int argi = 1;
    if (argc >= 2 && strcmp(argv[1], "-f") == 0) {
        is_replace = true;
        argi++;
    }

    if (argc - argi < 3) {
        fprintf(stderr, "Usage: inserter [-f] <file> <parent_inode> <storage.bin>\n");
        fprintf(stderr, "  -f : replace if the entry already exists\n");
        exit(1);
    }

    const char *target_path = argv[argi];
    const char *parent_str  = argv[argi + 1];
    const char *storage_path = argv[argi + 2];

    image_storage = malloc(4*1024*1024);
    file_buffer   = malloc(4*1024*1024);
    read_buffer   = malloc(4*1024*1024);
    if (image_storage == NULL || file_buffer == NULL || read_buffer == NULL) {
        fprintf(stderr, "inserter: out of host memory\n");
        exit(1);
    }

    FILE *fptr = fopen(storage_path, "rb+");
    if (fptr == NULL) {
        fprintf(stderr, "inserter: failed to open storage '%s'\n", storage_path);
        exit(1);
    }
    size_t read_count = fread(image_storage, 4*1024*1024, 1, fptr);
    if (read_count != 1) {
        fprintf(stderr, "inserter: failed to read storage (expected 4MiB)\n");
        fclose(fptr);
        exit(1);
    }
    FILE *fptr_target = fopen(target_path, "rb");
    if (fptr_target == NULL) {
        fprintf(stderr, "inserter: cannot open input file '%s'\n", target_path);
        fclose(fptr);
        exit(1);
    }
    size_t filesize = fread(file_buffer, 1, 4*1024*1024, fptr_target);
    fclose(fptr_target);

    const char *name = strrchr(target_path, '/');
    name = (name == NULL) ? target_path : name + 1;

    printf("Filename : %s\n",  name);
    printf("Filesize : %ld bytes\n", filesize);
    size_t filename_length = strlen(name);
    if (filename_length == 0 || filename_length > 255) {
        fprintf(stderr, "inserter: invalid file name '%s'\n", name);
        fclose(fptr);
        exit(1);
    }

    initialize_filesystem_ext2();
    struct EXT2DriverRequest request;
    struct EXT2DriverRequest reqread;
    printf("Filename       : %s\n", name);
    printf("Filename length: %zu\n", filename_length);

    request.buf = file_buffer;
    request.buffer_size = filesize;
    request.name = (char *) name;
    request.name_len = (uint8_t) filename_length;
    request.is_directory = false;
    sscanf(parent_str, "%u", &request.parent_inode);

    reqread = request;
    reqread.buf = read_buffer;
    int retcode = read(reqread);
    if (retcode == 0) {
        bool same = true;
        for (uint32_t i = 0; i < filesize; i++)
        {
            if (read_buffer[i] != file_buffer[i]){
                printf("not same\n");
                same = false;
                break;
            }
        }
        if (same) {
            printf("same\n");
        }
    }

    retcode = write(&request);
    if (retcode == 1 && is_replace) {
        retcode = delete (request);
        retcode = write(&request);
    }
    if (retcode == 0)
        puts("Write success");
    else if (retcode == 1)
        puts("Error: File/folder name already exist");
    else if (retcode == 2)
        puts("Error: Invalid parent node index");
    else
        puts("Error: Unknown error");

    fseek(fptr, 0, SEEK_SET);
    if (fwrite(image_storage, 4 * 1024 * 1024, 1, fptr) != 1) {
        fprintf(stderr, "inserter: failed to write storage\n");
        fclose(fptr);
        return 1;
    }
    fclose(fptr);

    return retcode == 0 ? 0 : 1;
}