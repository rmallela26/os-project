#include <ucontext.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <stdint.h>

#include "pcb.h"
#include "data_structs.h"
#include "fat/fat_commands.h"
#include "fs_system_calls.h"
#include "kernel_functions.h"

extern os_table_node* main_fd_table;
extern pcb* running_process;
extern pcb* foreground_process;

extern ucontext_t scheduler_thread;

extern int fs_fd;

extern int fat_size;
extern int block_size;

void truncate_file(int fblock, char* name);

/*
User level function. Opens a file. The allowed modes are F_WRITE (writing
and reading, truncates if file exists, creates if does not exist. Only one
instance can be opened in this mode), F_READ (reading only, error if file
doesn't exist), F_APPEND (open for reading and writing, creates if doesn't
exist, doesn't truncate if exists). Adds the file descriptor to the process's
fd_table and to the main table if necessary. The file will always be one in
the FAT fs.
*/
int f_open(char *fname, int mode) {
    int fd = -1;
    os_table_node* ptr = main_fd_table;
    os_table_node* entry_ptr;
    while(ptr != NULL) {
        if(strcmp(fname, ptr->name) == 0) { 
            //file exists, already open
            fd = ptr->fd;
            if(mode == F_WRITE && ptr->write_mode) return -1;
            else if(mode == F_WRITE) {
                ptr->write_mode = true;
                truncate_file(get_first_block_num(ptr->fentry_offset), fname);
            }
            ptr->num_open++;
            entry_ptr = ptr;
            break;
        }
        ptr = ptr->next;
    }
    
    if(fd == -1) {
        //add node to the main table
        os_table_node* new_node = malloc(sizeof(os_table_node));
        entry_ptr = new_node;

        if(main_fd_table == NULL) fd = 3; //0,1,2 are reserved for standards
        else fd = main_fd_table->fd + 1; //it will be reverse ordered (in terms of fd numbers)

        new_node->fd = fd;
        new_node->num_open = 1; 

        //create the file
        if(touch(fname) == -1) return -1;

        off_t file_entry = find_file_entry(fname);
        if(mode == F_WRITE) {
            new_node->write_mode = true;
            truncate_file(get_first_block_num(file_entry), fname);
        }

        new_node->permissions = 6; //defaults are read/write
        new_node->name = fname;
        new_node->fentry_offset = file_entry;
        
        new_node->next = main_fd_table;
        main_fd_table = new_node;
    }

    //add node to running_process fd table
    p_fd_table_node* new_node = malloc(sizeof(p_fd_table_node));
    new_node->fd = fd;
    new_node->main_entry_ptr = entry_ptr;
    new_node->mode = mode;
    new_node->offset = (fat_size + (get_first_block_num(entry_ptr->fentry_offset)-1)*block_size);

    //if there is another node from this process that has this file open
    //remove that node and decrement num open in main entry
    // p_fd_table_node* temp = running_process->fd_table;
    // bool flag = false;
    // if(temp->fd == fd) {
    //     printf("inside crash\n");
    //     flag = true;
    //     new_node->main_entry_ptr->num_open--;
    //     new_node->next = running_process->fd_table->next;
    //     free(running_process->fd_table);
    //     running_process->fd_table = new_node;
    // } else {
    //     while(temp->next != NULL) {
    //         if(temp->next->fd == fd) {
    //             printf("inside crash\n");
    //             p_fd_table_node* t = temp->next;
    //             temp->next = temp->next->next;
    //             t->main_entry_ptr->num_open--;
    //             free(t);
    //         }
    //     }
    // }
    // if(!flag) {
    //     new_node->next = running_process->fd_table;
    //     running_process->fd_table = new_node;
    // }
    new_node->next = running_process->fd_table;
    running_process->fd_table = new_node;
    return fd;
}

/*
Reads a number of bytes from a file. The file could be in the FAT or
it could be stdin.
Arguments:
    fd: The file descriptor to read from 
    n: The number of bytes to read
    buf: The buffer to read the bytes into
Returns:
    The number of bytes read
*/
int f_read(int fd, char *buf, int n) {
    p_fd_table_node* ptr = running_process->fd_table;
    while(ptr->fd != fd) ptr = ptr->next;

    if(fd == STDIN_FILENO && ptr->main_entry_ptr == NULL) { //no redirection just read
        //check if it is the foreground process, only fg process can read from stdin
        if(foreground_process != running_process) {
            //send stop signal
            k_process_kill(running_process, S_SIGSTOP, true);
            
            //schedule next thread to run
            swapcontext(&(running_process->execution_state), &scheduler_thread);
        }
        return read(fd, buf, n);
    } else {
        int bytes = read_file(n, buf, 0, 0, get_first_block_num(ptr->main_entry_ptr->fentry_offset), ptr->offset);
        int nread = 0;
        for(int i = 0; i < bytes; i++) {
            if(buf[i] == '\0') break;
            nread++;
        }
        
        ptr->offset += nread;
        return nread;
    }
}

/*
Write some number of bytes to a file. If mode is F_WRITE, write from
the offset stored in offset in fd_table node. If mode is F_APPEND,
find the end of the file and write from there. Increment the offset
variable by number of bytes written. 
Arguments:
    fd: The file descriptor to write to
    str: The string of characters to write. (Must be null terminated)
    n: The number of bytes to write
Returns:
    The number of bytes written 
*/
int f_write(int fd, char *str, int n) {
    p_fd_table_node* ptr = running_process->fd_table;
    while(ptr->fd != fd) ptr = ptr->next;

    if((fd == STDOUT_FILENO || fd == STDERR_FILENO) && ptr->main_entry_ptr == NULL) { //no redirection just read
        return write(fd, str, n);
    } else {
        uint16_t fblock = (ptr->offset - (ptr->offset % block_size))/block_size - (fat_size/block_size) + 1;
        int file_update = 0;
        if(ptr->mode == F_WRITE) {
            uint16_t b = fblock;
            //check if offset is at end of file
            //if it is then reading at offset should give '\0'
            char buf[1];
            lseek(fs_fd, ptr->offset, SEEK_SET);
            read(fs_fd, buf, 1);
            if(buf[0] == '\0') {
                //just appending to end of file
                file_update = n;
            } else {
                b = get_block_number(b);
                uint16_t bl = get_first_block_num(ptr->main_entry_ptr->fentry_offset);
                off_t end = find_end(&bl);
                if(b != 0xFFFF) { //not the last block
                    int num = block_size - (ptr->offset % block_size);
                    while(b != 0xFFFF) {
                        b = get_block_number(b);
                        num += block_size;
                    }
                    num += end % block_size;
                    if(n > num) file_update = n - num;
                } else {
                    //this is the last block
                    if(ptr->offset + n > end) file_update = (ptr->offset+n - end);
                    printf("file update is %d\n", file_update);
                }
            }
        } else {
            fblock = get_first_block_num(ptr->main_entry_ptr->fentry_offset);
            ptr->offset = find_end(&fblock);
            file_update = n;
        }
        // printf("file update is %d\n", file_update);
        int bytes = write_file(n, str, 0, 0, fblock, ptr->offset, false); //overwrite is only true when want to truncate the file at the end fo this string
        
        if(str[n-1] == '\0') {
            // printf("in null edit\n");
            ptr->offset += bytes-1;
        }
        else {
            // printf("offset was %ld, now it is %ld\n", ptr->offset, ptr->offset + bytes);
            ptr->offset += bytes; //update the file offset
        }
        //correct file size in directory block
        if(file_update != 0) prep_write(file_update, ptr->main_entry_ptr->name, false);

        // printf("the new file size is now %d\n", get_file_size(ptr->main_entry_ptr->fentry_offset));
        return bytes;
    }
}

/*
Close a file. It removes the node from the running process file table and
the node from the main fd table if there are 0 references to it. 
Arguments:
    fd: The file descriptor to close
Returns:
    Nothing
*/
void f_close(int fd) {
    //free the node in the process's fd table
    p_fd_table_node* ptr = running_process->fd_table;
    os_table_node* main_entry;

    bool write_mode = false;

    if(ptr->fd == fd) {
        if(ptr->mode == F_WRITE) write_mode = true;
        main_entry = ptr->main_entry_ptr;
        running_process->fd_table = ptr->next;
        free(ptr);
    } else {
        while(ptr->next != NULL && ptr->next->fd != fd) ptr = ptr->next;
        // if(ptr->next == NULL) return; //file was already closed

        if(ptr->next->mode == F_WRITE) write_mode = true;
        main_entry = ptr->next->main_entry_ptr;
        p_fd_table_node* temp = ptr->next->next;
        free(ptr->next);
        ptr->next = temp;
    }
    // printf("here\n");
    // if(fd == 3) printf("TRYNG TO CLOSE 3");
    if(main_entry == NULL) {
        // printf("HERe\n");
        return;
    }

    //decrement num open in main entry and see if it is 0
    //if it is, remove it from the table
    main_entry->num_open--;
    // if(write_mode) main_entry->write_mode = false;
    if(main_entry->num_open == 0) {
        os_table_node* mptr = main_fd_table;
        if(mptr == main_entry) {
            // free(mptr->name);
            // printf("here\n");
            main_fd_table = mptr->next;
            free(mptr);
            // printf("here\n");
        } else {
            while(mptr->next != main_entry) mptr = mptr->next;
            os_table_node* temp = mptr->next->next;
            free(mptr->next->name);
            free(mptr->next);
            mptr->next = temp;
        }
    }
}

/*
Remove a file. It removes a file only if there are 0 instances of it open. 
It can check this by simply checking if the file is in the main fd table
Arguments:
    fname: The name of the file to remove 
Returns:
    0 on success, -1 on error (if you try to close a file that is still in use)
*/
int f_unlink(char* fname) {
    os_table_node* mptr = main_fd_table;
    while(mptr != NULL && strcmp(mptr->name, fname) != 0) mptr = mptr->next;
   
    if(mptr != NULL) return -1;

    return remove_file(fname);
}

/*
Function similar to lseek. Reposition the offset of a file depending 
on the mode. If offset is big enough, might need to travel accross 
blocks to get to the right real offset in the file system (the offset
relative to fs_fd). Assumes offset is nonnegative.
TODO: implement F_SEEK_END 
Arguments:
    fd: The file descriptor of the file we are seeking
    offset: The offset to which to seek to
    whence: The mode. F_SEEK_SET: seek to the offset offset, F_SEEK_CUR: 
            seek to current offset + offset bytes, F_SEEK_END: seek to 
            end of the file plus offset bytes
Returns:
    Nothing
*/
void f_lseek(int fd, int offset, int whence) {
    p_fd_table_node* ptr = running_process->fd_table;
    while(ptr->fd != fd) ptr = ptr->next;

    if(whence == F_SEEK_SET) {
        int block = get_first_block_num(ptr->main_entry_ptr->fentry_offset); 
        while(offset > block_size) {
            block = get_block_number(block);
            offset -= block_size;
        }
        ptr->offset = (fat_size + (block-1)*block_size) + offset;
        printf("OFFSET IS %ld\n", ptr->offset);
    }
    else if(whence == F_SEEK_CUR) {
        if(offset < (block_size - ptr->offset % block_size)) {
            ptr->offset += offset; 
            return;
        }

        offset -= (block_size - ptr->offset % block_size);
        int block = ((ptr->offset - (ptr->offset % block_size))/block_size - (fat_size/block_size) + 1) + 1;
        while(offset > block_size) {
            offset -= block_size;
            block = get_block_number(block);
        }

        ptr->offset = (fat_size + (block-1) * block_size) + offset;
    } 
}

/*
Truncates a file. Does this by writing the null character to it in overwrite
mode.
Arguments:
    fblock: The first block of the file to be truncated
*/
void truncate_file(int fblock, char* name) {
    off_t offset = fat_size + (fblock-1)*block_size;
    char* str = "";
    write_file(1, str, 0, 0, fblock, offset, true);
    prep_write(1, name, true);
}

/*
User level function similar to dup2. Redirect anything coming to/from 
one file descriptor to another. When this happens, change the num_open
in the main fd table accordingly. Redirection for the calling process
(which is running_process)
Arguments:
    oldfd: The fd that is being redirected
    newfd: The fd that oldfd is redirecting to 
Returns:
    0 on success, -1 on error
*/
int duplicate(int oldfd, int newfd, pcb* process) {
    //find the fd_table entries
    p_fd_table_node* old_ptr = process->fd_table;
    p_fd_table_node* new_ptr = process->fd_table;

    while(old_ptr != NULL && old_ptr->fd != oldfd) old_ptr = old_ptr->next;
    while(new_ptr != NULL && new_ptr->fd != newfd) new_ptr = new_ptr->next;

    if(old_ptr == NULL || new_ptr == NULL) return -1;

    //could be null if they are stdin, stdout, stderr
    if(old_ptr->main_entry_ptr != NULL) old_ptr->main_entry_ptr->num_open--;
    if(new_ptr->main_entry_ptr != NULL) new_ptr->main_entry_ptr->num_open++;

    // printf("IN DUPLICATE\n");
    old_ptr->mode = new_ptr->mode;
    old_ptr->offset = new_ptr->offset;
    old_ptr->main_entry_ptr = new_ptr->main_entry_ptr;
    // printf("old name now %s\n", old_ptr->main_entry_ptr->name);
    return 0;
}