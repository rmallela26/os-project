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


// int main(void) {
//     char* fs_name = "fat_fs";
//     mount(fs_name);

//     chmod(2, "newer.txt");
//     ls();
// }