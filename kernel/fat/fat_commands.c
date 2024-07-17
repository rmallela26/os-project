#include <ucontext.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <ctype.h>

#include "fat_commands.h"
#include "../fs_system_calls.h"

#define END 0
#define END_DELETED 3
#define IN_USE 2
#define DELETED 1

extern int fs_fd;
extern int fat_size;
extern int block_size;

extern file_system* fsystems;
extern uint16_t *fat;

extern int directory_offset;

uint16_t get_block_number(uint16_t block);
void put_block_number(uint16_t block, uint16_t item);
off_t find_file_entry(char* name);
off_t find_end(uint16_t* fblock);
uint16_t get_first_block_num(off_t offset);

/*
This function is called by the mkfs terminal command. It will create 
the FAT file system as a file on the host OS. If mkfs is called with 
a file name that already exists for another file system, that file system
will get wiped. 
Arguments:
    name: The name of the file system. It will be the name of the file 
        on the host OS that represents the FAT file system
    num_blocks: The number of blocks in the FAT, ranging from 1 to 32
    block_size: The size of each block, ranges from 1 to 4, which corresponds
        to 512, 1024, 2048, 4096 bytes respectively. 
*/
void mkfs(char* name, uint8_t num_blocks, uint8_t block_size_config) {
    // file_system* fs = malloc(sizeof(file_system));
    // fs->name = name;
    // fs->next = fsystems;
    // fsystems = fs;

    fs_fd = open(name, O_RDWR | O_CREAT | O_TRUNC); //delete anything that was already there
    fs_fd = open(name, O_RDWR | O_APPEND);
    if(fs_fd == -1) perror("error opending file system file");

    fat_size = num_blocks * block_size_config;

    //initialize fat 
    uint8_t buffer[2];
    buffer[0] = num_blocks;
    buffer[1] = block_size_config;

    for(int i = 0; i < fat_size/2; i++) {
        if(i == 0) write(fs_fd, buffer, 2); //the meta data for the fs 
        else { //initialize the rest of the entries with 0's (signifying free blocks)
            buffer[0] = 0;
            buffer[1] = 0;
            write(fs_fd, buffer, 2);
        }
    }
}

/*
Function mounts one of the file systems into memory using mmap. 
It puts the FAT into memory. 
Arguments:
    name: The name of the file system to be mounted
Returns:
    Nothing
*/
void mount(char* name) {
    //initialize globals
    fs_fd = open(name, O_RDWR);
    uint8_t meta[2];
    read(fs_fd, meta, 2);

    if(meta[1] == 1) block_size = 512;
    else if(meta[1] == 2) block_size = 1024;
    else if(meta[1] == 3) block_size = 2048;
    else block_size = 4096;
    
    fat_size = meta[0] * block_size;
    // printf("%d %d\n", fat_size, block_size);
    


    // file_system* ptr = fsystems;
    // while(strcmp(ptr->name, name) != 0) ptr = ptr->next;

    // fs_fd = ptr->fd;
    // fat_size = ptr->fat_size;
    // // directory_offset = fat_size;
    // block_size = ptr->block_size;

    // printf("fat_size: %d\n", fat_size);
    // printf("block_size: %d\n", block_size);

    fat = mmap(NULL, fat_size, PROT_READ | PROT_WRITE, MAP_SHARED, fs_fd, 0);
    if(fat == MAP_FAILED) perror("error creating mapping");
}

/*
Unmounts the currently mounted filesystem. 
Arguments:
    None
Returns:
    Nothing
*/
void umount(void) {
    if(munmap(fat, fat_size) == -1) perror("error unmounting fat");
}

int find_free_block(void) {
    for(int i = 4; i < fat_size/2; i += 2) {
        // printf("fat[%d]: %x fat[%d]: %x\n", i/2, fat[i], i/2+1, fat[i+1]);
        if(fat[i] == 0 && fat[i+1] == 0) return i/2;
    }
    return -1;
}

/*
Creates a new file if the file doesn't already exist, otherwise
updates the file's timestamp to the current system time. To 
create a file, it updates the directory block and allocates
a free block in the FAT. If the directory block is full, it 
allocates a new directory block in the FAT and then allocates a 
block in the FAT. Each entry in the directory block is 64 bytes. 
name[0] = 3 -> end of directory and that entry is deleted
name[0] = 0 -> end of directory
name[0] = 1 -> deleted entry
name[0] = 2 -> entry in use
Arguments:
    file: The null terminated name of the file to be created/updated
Returns:
    0 on success, -1 on error
*/
int touch(char* name) {
    if(strlen(name) > 30) { //one bit for entry info, one for null terminator
        perror("name too long");
        return -1;
    } 

    for(int i = 0; i < strlen(name); i++) {
        if(!(isalnum(name[i]) || name[i] == '.' || name[i] == '_' || name[i] == '-')) {
            perror("name contains invalid characters");
            return -1;
        }
    }

    //find if the file already exists 
    //if it does, update its time stamp
    off_t offset = find_file_entry(name);
    if(offset != -1) {
        //file exists just update time stamp
        time_t sys_time = time(NULL);
        offset += 40;
        lseek(fs_fd, offset, SEEK_SET);
        write(fs_fd, &sys_time, sizeof(time_t));
        return 0;
    }

    //find where an opening for a file entry is
    int block = 2;
    int i = fat_size;
    bool flag = false; //represents that we reached the last entry
                        //start the next round to make sure there is space
                        //for the next entry and allocate if necessary
    while(1) {
        if(i != fat_size && i % block_size < 64) {
            //go to next directory block 
            if(fat[block] == 0xFF && fat[block+1] == 0xFF) {
                //create new block
                int free_block = find_free_block();
                if(free_block == -1) {
                    perror("no free blocks");
                    return -1;
                }
                put_block_number(block, free_block);

                fat[free_block] = 0xFF;
                fat[free_block+1] = 0xFF;
                i = fat_size + block_size * (free_block-1);
            } else {
                block = get_block_number(block);
                i = fat_size + block_size * (block-1);
            }
        }
        if(flag) break;
        lseek(fs_fd, i, SEEK_SET);
        uint8_t buffer[1];
        if(read(fs_fd, buffer, 1) == 0 || buffer[0] == END_DELETED) { //there are no entries at all or the end entry got deleted
            flag = true; 
            break;
        }
        if(buffer[0] == IN_USE) { //entry is in use
            i += 64;
            continue;
        } else if(buffer[0] == END) {//last entry
            lseek(fs_fd, i, SEEK_SET);
            uint8_t info_bit = IN_USE;
            write(fs_fd, &info_bit, 1);
            i += 64;
            flag = true;
        } else break;
    }

    //add the new entry infromation bit
    lseek(fs_fd, i, SEEK_SET);
    uint8_t info_bit;
    if(flag) info_bit = END;
    else info_bit = IN_USE;
    write(fs_fd, &info_bit, 1);
    // if(flag) write(fs_fd, "0", 1);
    // else write(fs_fd, "2", 1);

    //file name
    lseek(fs_fd, i+1, SEEK_SET);
    if(write(fs_fd, name, strlen(name)+1) == -1) perror("error writing");

    //num bytes in file
    i += 32;
    lseek(fs_fd, i, SEEK_SET);
    uint8_t buf[4];
    buf[0] = buf[1] = buf[2] = buf[3] = 0;
    // uint32_t num_byte = 0;
    if(write(fs_fd, buf, 4) == -1) perror("error writing");

    //first block
    int free_block = find_free_block();
    put_block_number(free_block, 0xFFFF);
    // if(free_block == 4) printf("free block 4 has inside: %x", get_block_number(4));

    i += 4;
    uint8_t buffer[2];
    buffer[0] = free_block >> 8;
    buffer[1] = free_block & 255;

    lseek(fs_fd, i, SEEK_SET);
    if(write(fs_fd, buffer, 2) == -1) perror("error writing");

    //type
    i += 2;
    uint8_t type = 1;
    lseek(fs_fd, i, SEEK_SET);
    if(write(fs_fd, &type, 1) == -1) perror("error writing");

    //permissions
    i += 1;
    uint8_t permissions = 6; //default is read/write
    lseek(fs_fd, i, SEEK_SET);
    if(write(fs_fd, &permissions, 1) == -1) perror("error writing");

    //timestamp
    i += 1;
    time_t sys_time = time(NULL);
    lseek(fs_fd, i, SEEK_SET);
    if(write(fs_fd, &sys_time, sizeof(time_t)) == -1) perror("error writing");

    return 0;
}

/*
Lists all files in the directory. Prints to stdout
Arguments:
    None
Returns:
    Nothing
*/
void ls(void) {
    int i = fat_size;
    int block = 2;
    while(1) {
        if(i != fat_size && i % block_size < 64) {
            block = get_block_number(block);
            i = fat_size + block_size * (block-1);
        }

        lseek(fs_fd, i, SEEK_SET);
        uint8_t buf[1];
        // printf("%d\n", i == (fat_size+64));
        if(read(fs_fd, buf, 1) == 0) return;
        if(buf[0] == DELETED) { //deleted entry
            i += 64;
            continue;
        }

        if(buf[0] == END_DELETED) break; //last entry is a deleted one
        
        char name[31];
        uint8_t permissions;
        time_t sys_time;
        uint8_t block_num[2];

        read(fs_fd, name, 31);
        i += 36;
        lseek(fs_fd, i, SEEK_SET);
        read(fs_fd, block_num, 2);
        i += 3;
        lseek(fs_fd, i, SEEK_SET);
        read(fs_fd, &permissions, 1);
        read(fs_fd, &sys_time, sizeof(time_t));

        char* perms;
        if(permissions == 0) perms = "---\0";
        else if(permissions == 2) perms = "-w-\0";
        else if(permissions == 4) perms = "r--\0";
        else if(permissions == 5) perms = "r-x\0";
        else if(permissions == 6) perms = "rw-\0";
        else perms = "rwx\0";

        struct tm *time_info;
        time_info = localtime(&sys_time);
        char time_string[100];

        strftime(time_string, sizeof(time_string), "%b %d %H:%M", time_info);

        int buffer_size = snprintf(NULL, 0, "%x %s %s %s\n", (block_num[0] << 8 | block_num[1]), perms, time_string, name);
        char* formatted_string = malloc(buffer_size+1);
        sprintf(formatted_string, "%x %s %s %s\n", (block_num[0] << 8 | block_num[1]), perms, time_string, name);
        f_write(STDOUT_FILENO, formatted_string, buffer_size);
        // printf("%x %s %s %s\n", (block_num[0] << 8 | block_num[1]), *perms, time_string, name);

        i += 25;
        if(buf[0] == END) {
            break;
        }
    }
}

uint16_t get_block_number(uint16_t block) {
    block *= 2;
    return (fat[block] << 8) | fat[block+1];
}

void put_block_number(uint16_t block, uint16_t item) {
    block *= 2;
    fat[block] = item >> 8;
    fat[block+1] = item & 255;
}

/*
Finds a file in the directory block and returns the offset
to get to that entry in the directory block (not the file itself)
Arguments:
    name: The name of the file we are looking for
Returns:
    The offset of the directory entry in the file system or -1 if the 
    file doesn't exist
*/
off_t find_file_entry(char* name) {
    off_t i = fat_size;
    int block = 2;
    while(1) {
        if(i != fat_size && i % block_size < 64) {
            //go to next block
            block = get_block_number(block);
            i = fat_size + block_size * (block-1);
        }

        lseek(fs_fd, i, SEEK_SET);
        uint8_t buf[1];
        // printf("%d\n", i == (fat_size+64));
        if(read(fs_fd, buf, 1) == 0) return -1;
        if(buf[0] == DELETED) { //deleted entry
            i += 64;
            continue;
        }

        if(buf[0] == END_DELETED) return -1; //last entry is a deleted one

        char f_name[31];
        read(fs_fd, f_name, 31);
        if(strcmp(f_name, name) == 0) return i;

        if(buf[0] == END) return -1; //just saw the last entry, no more entries
        
        i += 64;
    }
}

/*
Renames a file. 
Arguments:
    source: The original null terminated name of the file
    destination: The new null terminated name for the file 
Returns:
    0 on success, -1 on failure
*/
int move(char* source, char* destination) {
    if(strlen(destination) > 30) { //one bit for entry info, one for null terminator
        perror("destination too long");
        return -1;
    } 

    for(int i = 0; i < strlen(destination); i++) {
        if(!(isalnum(destination[i]) || destination[i] == '.' || destination[i] == '_' || destination[i] == '-')) {
            perror("destination contains invalid characters");
            return -1;
        }
    }

    //get the offset to the directory entry
    off_t offset = find_file_entry(source);
    if(offset == -1) {
        perror("file doesn't exist");
        return -1;
    }

    //skip the info bit
    lseek(fs_fd, offset+1, SEEK_SET);
    write(fs_fd, destination, (strlen(destination)+1) * sizeof(char));
    return 0;
}

/*
Removes a file. Needs to update the FAT and the directory block. When
it updates the directory block, it changes the info bit to DELETED if 
it was previously IN_USE, otherwise it changes it to END_DELETED if it 
was previously END
Arguments:
    name: The name of the file to be removed
Returns:
    0 on success, -1 on error
*/
int remove_file(char* name) {
    off_t offset = find_file_entry(name);
    if(offset == -1) {
        perror("file doesn't exist");
        return -1;
    }

    uint8_t buf[1];
    uint8_t block_num[2];
    lseek(fs_fd, offset, SEEK_SET);
    read(fs_fd, buf, 1);

    //write info bit
    if(buf[0] == END) buf[0] = END_DELETED;
    else buf[0] = DELETED;
    lseek(fs_fd, offset, SEEK_SET);
    write(fs_fd, buf, 1);

    //get the first block
    lseek(fs_fd, offset+36, SEEK_SET);
    read(fs_fd, block_num, 2);

    //find the block in FAT, chase down its links and set them to 0
    int block = block_num[0] << 8 | block_num[1];
    int next_block;
    while((next_block = get_block_number(block)) != 0xFFFF) {
        put_block_number(block, 0);
        block = next_block;
    }
    put_block_number(block, 0);
    return 0;
}

/*
Read a number of bytes from a file into a buffer. The file could 
be of three types: from the host OS, from the FAT filesystem, or from
stdin. 
Arguments:
    n: The number of bytes to read.
    buf: The buffer to read the characters into. Assumes the n bytes
        can fit into the buffer
    mode: 0 if file is from FAT filesystem, 1 if file is from the host OS
        of stdin
    fd: The file descriptor. Only valid if the mode is 1
    block_num: The block of the file that offset is in. Only valid if mode is 0
    file_offset: The file offset to start reading from. Valid for both.
        If it is -1, don't set the offset 
Returns:
    The number of bytes read
*/
int read_file(int n, char* buf, int mode, int fd, uint16_t block_num, off_t file_offset) {

    if(mode == 1) {
        if(file_offset != -1) lseek(fd, file_offset, SEEK_SET);
        int bytes = read(fd, buf, n);
        return bytes;
    } else {

        if(n > (block_size - (file_offset % block_size))) {
            //read spans multiple blocks
            int bytes = 0;
            while(n > 0) {
                int num;
                if(n > (block_size - (file_offset % block_size))) num = (block_size - (file_offset % block_size));
                else num = n;

                lseek(fs_fd, file_offset, SEEK_SET);
                int num_read = read(fs_fd, buf + bytes, num);

                bytes += num_read;
                if(num_read != num || num == n) return bytes; //we've reached the end of the file
                n -= num_read;

                //find the next block in the FAT, set file_offset and block_num
                //accordingly
                block_num = get_block_number(block_num);
                if(block_num == 0xFFFF) return bytes; //that was the end of the file
                file_offset = fat_size + (block_num-1)*block_size;
            }
            return bytes;
        }
        else{
            // printf("file offset is %lu\n", file_offset);
            lseek(fs_fd, file_offset, SEEK_SET);
            int bytes = read(fs_fd, buf, n);
            return bytes;
        }
    }
}

/*
Writes a number of bytes into a file. Can overwrite or append. There
are two types of files: FAT filesystem files and stdout. If bytes is
greater than how long the file is, allocate new blocks. 
It is the responsibility of the caller to create the file if it doesn't exist. 
It is also the responsibility of the caller to update the size parameter
in the directory block. 
It is also the responsibility of the caller to make sure file_offset is 
in the right place so that it writes over the null terminator that was
there before. 
Arguments:
    n: The number of bytes to write
    buf: The buffer that holds the characters to write
    mode: 0 if file is a FAT filesystem file, 1 if it is stdout or if file
        is on host os
    fd: The file descriptor of the output file. Only valid for mode 1
    block_num: The block of the file that offset is in. Only valid if mode is 0.
    file_offset: The file offset to start writing to. Valid for both.
        Only valid for mode 0. If it is 0, naturally means overwrite
        existing file. 
    overwrite: Whether to overwrite or not. Needed because file_offset argument
        is changed throughout the function. Only valid for mode 0. 
Returns:
    The number of bytes written. 
*/
int write_file(int n, char* buf, int mode, int fd, uint16_t block_num, off_t file_offset, bool overwrite) {
    // printf("n: %d, buf: %s, block_num: %d, offset: %ld, overwrite: %d\n", n, buf, block_num, file_offset, overwrite);
    if(mode == 1) {
        int bytes = write(fd, buf, n);
        return bytes;
    } else {
        if(n > (block_size - (file_offset % block_size))) {
            //write spans multiple blocks
            int bytes = 0;
            while(n > 0) {
                int num;
                if(n > (block_size - (file_offset % block_size))) num = (block_size - (file_offset % block_size));
                else num = n;
                // printf("num is %d\n", num);
                lseek(fs_fd, file_offset, SEEK_SET);
                int num_write = write(fs_fd, buf + bytes, num);
                bytes += num_write;

                if(num_write != num || num == n) {
                    //we've reached the end of the file

                    //if mode was overwrite, free any remaining blocks in the 
                    //FAT that might come after this block (because file may
                    //not be as long as before)
                    if(overwrite && get_block_number(block_num) != 0xFFFF) {
                        int next_block = get_block_number(block_num);
                        put_block_number(block_num, 0xFFFF);
                        while(next_block != 0xFFFF) {
                            printf("here\n");
                            block_num = next_block;
                            next_block = get_block_number(block_num);
                            put_block_number(block_num, 0);
                        }
                        put_block_number(block_num, 0);
                    }
                    return bytes;
                } 
                n -= num_write;

                //find the next block in the FAT, set file_offset and block_num
                //accordingly
                uint16_t next_block = get_block_number(block_num);

                if(next_block == 0xFFFF) { //need to allocate a new block
                    next_block = find_free_block();
                    // printf("curr num: %d block found: %d\n", block_num, next_block);
                    put_block_number(block_num, next_block);
                    put_block_number(next_block, 0xFFFF);
                }
                block_num = next_block;
                file_offset = fat_size + (block_num-1)*block_size;
            }
            return bytes;
        }
        else {
            // printf("else\n");
            // lseek(fs_fd, file_offset-33, SEEK_SET);
            // char jijij[33];
            // read(fs_fd, jijij, 33);
            // printf("jijij %s\n", jijij);
            lseek(fs_fd, file_offset, SEEK_SET);
            int bytes = write(fs_fd, buf, n);

            if(overwrite && get_block_number(block_num) != 0xFFFF) {
                printf("REMOVING BLOCKS\n");
                int next_block = get_block_number(block_num);
                put_block_number(block_num, 0xFFFF);
                while(next_block != 0xFFFF) {
                    block_num = next_block;
                    next_block = get_block_number(block_num);
                    put_block_number(block_num, 0);
                }
                put_block_number(block_num, 0);
            }
            // printf("bytes written %d\n", bytes);
            return bytes;
        }
    }
}

/*
The cat program from bash. Concatenate files together or input from stdin 
and either output to standard out or to a file using write or append mode.
If the output file doesn't exist, then it will be created. Can also cat
singular files from host OS. 
Arguments:
    num_inputs: The number of input files.
    names: Pointer to string array of file names to concatenate. Will be NULL
        if reading from stdin.
    overwrite: True if overwrite, false if append
    output_name: Name of the output file. Will be NULL if output is to 
    stdout
    fd: The file descriptor in the host OS to read from. If -1, num_inputs isn't
        0. Read from FAT. If it isn't -1, that it is either STDIN_FILENO, or
        it means read from some file in the host OS. 
Returns:
    Nothing
*/
void concatenate(int num_inputs, char** names,  bool overwrite, char* output_name, int fd) {
    if(fd != -1) {
        //read from stdin indefinitely 
        char buf[block_size];
        int num_bytes;
        while((num_bytes = read(fd, buf, block_size-1)) != 0) {
            // char buf[block_size];
            // int num_bytes = read(STDIN_FILENO, buf, block_size-1);
            buf[num_bytes] = '\0';
            if(output_name == NULL) {
                write(STDOUT_FILENO, buf, num_bytes);
            } else {
                off_t file_offt = find_file_entry(output_name);
                if(file_offt == -1) {
                    // printf("CREATING FILE\n");
                    //create the file
                    touch(output_name);
                    file_offt = find_file_entry(output_name); 
                }

                uint8_t size_buf[4];
                uint32_t num = num_bytes;
                if(!overwrite) {
                    lseek(fs_fd, file_offt + 32, SEEK_SET);
                    read(fs_fd, size_buf, 4);
                    num += ((size_buf[0] << 24 | size_buf[1] << 16) | size_buf[2] << 8) | size_buf[3];
                    // if(oldsize > 0) num += oldsize; //if something was there, null char already counted
                    // else num += 1; //if nothing was there, count 1 more for null char
                    // if(num != file_size) num--; //need to remove 1 for the null char, because it was counted in the text already in the file

                }
                if(num == num_bytes) num += 1; //nothing was there before, so need to count null char
                printf("file size concat is %d\n", num);
                size_buf[0] = num >> 24;
                size_buf[1] = (num >> 16) & 255;
                size_buf[2] = (num >> 8) & 255;
                size_buf[3] = num & 255;

                // file_offt = find_file_entry(output_name);
                lseek(fs_fd, file_offt + 32, SEEK_SET);
                write(fs_fd, size_buf, 4);

                uint16_t fblock = get_first_block_num(file_offt);
                off_t write_start = fat_size + (fblock-1)*block_size;
                //if this was the first time writing, check if overwrite is true
                //if overwrite is true, just set the write start to the start of the block
                //if overwrite is false OR this is not the first time, then get end of 
                //the file and send overwrite as false. 
                if(overwrite) {
                    write_file(num_bytes+1, buf, 0, 0, fblock, write_start, true);
                    overwrite = false;
                    continue; 
                }

                write_start = find_end(&fblock);
                write_file(num_bytes+1, buf, 0, 0, fblock, write_start, false); //overwrite is false regardless
            }
        }
    } else {
        //concatenate files
        //then either print to stdout or write to a file 
        //create the file if necessary
        for(int i = 0; i < num_inputs; i++) {
            char* curr = names[i];
            off_t file_offt = find_file_entry(curr);
            if(file_offt == -1) {
                perror("file doesn't exist");
                return;
            }

            uint8_t size_buf[4];
            lseek(fs_fd, file_offt + 32, SEEK_SET);
            read(fs_fd, size_buf, 4);
            int file_size = ((size_buf[0] << 24 | size_buf[1] << 16) | size_buf[2] << 8) | size_buf[3];
            // printf("file size is: %d\n", file_size);
            char buffer[file_size]; //want the null terminator because helps find the end of the file correctly 
            // printf("file size is %d\n", file_size);
            uint16_t fblock = get_first_block_num(file_offt);
            off_t read_start = fat_size + (fblock-1)*block_size;
            read_file(file_size, buffer, 0, 0, fblock, read_start);

            //parse for nulls
            int bytes_to_write = 1; //the null char
            for(int i = 0; i < file_size; i++) {
                if(buffer[i] == '\0') break;
                bytes_to_write++;
            }
            if(output_name == NULL) {
                //write to standard out
                // printf("\n\n\n");
                // write(STDOUT_FILENO, buffer, file_size);
                write(STDOUT_FILENO, buffer, bytes_to_write);
            } else {
                off_t outfile = find_file_entry(output_name);
                if(outfile == -1) {
                    touch(output_name);
                    outfile = find_file_entry(output_name);
                }

                //update file size of output file
                // uint32_t num = file_size; 
                uint32_t num = bytes_to_write; 
                if(!(overwrite && i == 0)) {
                    lseek(fs_fd, outfile + 32, SEEK_SET);
                    read(fs_fd, size_buf, 4);
                    num += ((size_buf[0] << 24 | size_buf[1] << 16) | size_buf[2] << 8) | size_buf[3];
                    // if(num != file_size) num--; //need to remove 1 for the null char, because it was counted in the text already in the file
                    if(num != bytes_to_write) num--; //need to remove 1 for the null char, because it was counted in the text already in the file
                    // printf("number num nummington is %d\n", num);
                }
                // printf("bytes in output file: %d\n", num);
                size_buf[0] = num >> 24;
                size_buf[1] = (num >> 16) & 255;
                size_buf[2] = (num >> 8) & 255;
                size_buf[3] = num & 255;
                lseek(fs_fd, outfile + 32, SEEK_SET);
                write(fs_fd, size_buf, 4);
                //prep and write content to file
                fblock = get_first_block_num(outfile);
                off_t write_start = fat_size + (fblock-1)*block_size;
                if(i == 0) {
                    if(!overwrite) write_start = find_end(&fblock);
                    else {
                        // write_file(file_size, buffer, 0, 0, fblock, write_start, true);
                        write_file(bytes_to_write, buffer, 0, 0, fblock, write_start, true);
                        continue;
                    }
                } else write_start = find_end(&fblock);
                // printf("buffer is %s\n", buffer);
                // write_file(file_size, buffer, 0, 0, fblock, write_start, false);
                write_file(bytes_to_write, buffer, 0, 0, fblock, write_start, false);
            }
        }
    }
}

/*
Find the offset to the end of a file.
Arguments:
    fblock: The first block number of the file
Returns:
    The offset to the end of the file
*/
off_t find_end(uint16_t* fblock) {
    //This will get the last block, and then in that block find the first
    //null char. There won't be null chars in earlier blocks because 
    //when we write, if overwrite, the subsequent blocks are deleted, 
    //and if append, we would find the first null char the first time. 

    // printf("fblock is %d\n", *fblock);
    // printf("number in fblock entry is %x\n", get_block_number(fnum));
    while(get_block_number(*fblock) != 0xFFFF) *fblock = get_block_number(*fblock);
    // printf("after while\n");
    off_t offset = fat_size + ((*fblock)-1)*block_size;

    //read the block size and check how many bytes we read
    lseek(fs_fd, offset, SEEK_SET);
    char buf[block_size];
    int bytes = read(fs_fd, buf, block_size);
    // printf("down below\n");
    // for(int i = 0; i < bytes; i++) {
    //     if(buf[i] != '\0') printf("%c", buf[i]);
    // }
    if(bytes != 0) {
        for(int i = 0; i < bytes; i++) {
            if(buf[i] == '\0') break;
            offset++;
        }
    }
    // if(bytes > 0) offset += bytes-1; //last one is null terminator
    // offset += (bytes - 1); //last one is the null terminator
    return offset;
}

/*
Get the first block number of a file from the directory block
Arguments:
    offset: The offset to the file entry in the directory block
Returns:
    The first block number of the file in the file entry pointed to 
    by offset
*/
uint16_t get_first_block_num(off_t offset) {
    lseek(fs_fd, offset + 36, SEEK_SET);
    uint8_t buffer[2];
    read(fs_fd, buffer, 2);
    uint16_t block_num = buffer[0] << 8 | buffer[1];
    return block_num;
}

/*
Function to copy a file from a source to a destination. Mode 0 is
both files are in FAT filesystem, 1 is input is in host OS output
in FAT, and 2 is input in FAT and output in host OS. 
Arguments:
    input: The name of the input file
    output: The name of the output file
    mode: The mode
Returns:
    Nothing
*/
void copy(char* input, char* output, int mode) {
    if(mode == 0) {
        //both files in FAT
        concatenate(1, &input, true, output, -1);
    } else if(mode == 1) {
        int fd = open(input, O_RDONLY);
        concatenate(0, NULL, true, output, fd);
    } else{
        //input in FAT, output in host
        off_t file_entry = find_file_entry(input);
        if(file_entry == -1) {
            perror("input doesn't exist");
            return;
        }
        
        //find file size
        uint8_t size_buf[4];
        lseek(fs_fd, file_entry + 32, SEEK_SET);
        read(fs_fd, size_buf, 4);
        int file_size = ((size_buf[0] << 24 | size_buf[1] << 16) | size_buf[2] << 8) | size_buf[3];

        char buffer[file_size];
        uint16_t fblock = get_first_block_num(file_entry);
        read_file(file_size, buffer, 0, 0, fblock, fat_size + (fblock-1)*block_size);

        int fd = open(output, O_WRONLY);
        write(fd, buffer, file_size);
    }
}

/*
Change the permissions of a file. 
Arguments:
    new_mode: The new mode of the file. It is a number 0-7
    name: The name of the file whose permissions are being changed
Returns:
    0 on success, -1 on error
*/
int chmod(uint8_t new_mode, char* name) {
    off_t file_entry = find_file_entry(name);
    if(file_entry == -1) {
        perror("file doesn't exist");
        return -1;
    }

    lseek(fs_fd, file_entry + 39, SEEK_SET);
    write(fs_fd, &new_mode, 1);
    return 0;
}

/*
Function to prepr write calls. Updates the size in the directory block
based on how many bytes to write and whether the file should be overwritten
or not. 
NOTE: Allocating/deallocating blocks is handled by write. This function
is simply to update the file size in the directory block. 
Arguments:
    bytes: The nubmer of bytes to write
    name: The name of the file we are writing to
    overwrite: Whether overwriting or not
Returns:
    The new file size
*/
int prep_write(int bytes, char* name, bool overwrite) {
    off_t file_entry = find_file_entry(name);
    if(file_entry == -1) {
        perror("file doesn't exist");
        return -1;
    }

    int file_size = bytes;
    if(overwrite) {
        lseek(fs_fd, file_entry + 32, SEEK_SET);
        uint8_t size_buf[4];
        size_buf[0] = file_size >> 24;
        size_buf[1] = (file_size >> 16) & 255;
        size_buf[2] = (file_size >> 8) & 255;
        size_buf[3] = file_size & 255;
        write(fs_fd, size_buf, 4);
    } else {
        lseek(fs_fd, file_entry + 32, SEEK_SET);
        uint8_t size_buf[4];
        read(fs_fd, size_buf, 4);
        file_size += ((size_buf[0] << 24 | size_buf[1] << 16) | size_buf[2] << 8) | size_buf[3];
        if(file_size != bytes) file_size--; //remove one of the null chars

        size_buf[0] = file_size >> 24;
        size_buf[1] = (file_size >> 16) & 255;
        size_buf[2] = (file_size >> 8) & 255;
        size_buf[3] = file_size & 255;
        
        lseek(fs_fd, file_entry+32, SEEK_SET);
        write(fs_fd, size_buf, 4);
    }

    return file_size;
}

//offset: the offset to the file in the directory block 
int get_file_size(off_t offset) {
    uint8_t size_buf[4];
    lseek(fs_fd, offset+32, SEEK_SET);
    read(fs_fd, size_buf, 4);
    int num = ((size_buf[0] << 24 | size_buf[1] << 16) | size_buf[2] << 8) | size_buf[3];
    return num;
}