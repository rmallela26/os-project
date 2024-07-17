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

#include "fat_commands.h"

int fs_fd;
int fat_size;
int block_size;

uint16_t *fat = NULL;

int main(void) {
    char* fs_name = "fat_fs";
    // mkfs(fs_name, 32, 1);
    mount(fs_name);

    chmod(2, "newer.txt");
    ls();
}