#include <ucontext.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>

#include "../fs_system_calls.h"
#include "../user_functions.h"
#include "../fat/fat_commands.h"

void free_argv(char* argv[]);

void shell_ls(void) {
    ls();
    p_exit();
}

/*
argv: First argument is "0" if it is append, "1" if overwrite. All other
arguments except the last two are the input files. The second to last is 
the output file. The last is NULL. The second to last will be blank if write
is to stdout. There won't be any middle args if read is from stdin
*/
void cat(char* argv[]) {
    int num_args = 0;
    for(int i = 0; argv[i] != NULL; i++) {
        // printf("arg %d is %s\n", i, argv[i]);
        num_args++;
    }
    while(argv[num_args] != NULL) num_args++;

    if(strlen(argv[num_args-1]) == 0) argv[num_args-1] = NULL;
    
    if(num_args == 2) {
        //read from stdin write to file
        // printf("here\n");
        concatenate(0, NULL, atoi(argv[0]), argv[num_args-1], STDIN_FILENO);
    } else {
        // printf("down %d\n", num_args);
        concatenate(num_args-2, argv+1, atoi(argv[0]), argv[num_args-1], -1);
    }
    free_argv(argv); //mem leak if concatenate doesn't return from here; fix
    p_exit();
}

void shell_busy(void) {
    while(1);
}

void shell_echo(char* argv[]) {
    // write(STDOUT_FILENO, argv[0], strlen(argv[0]));
    for(int i = 0; argv[i] != NULL; i++) {
        f_write(STDOUT_FILENO, argv[i], strlen(argv[i])+1);
        f_write(STDOUT_FILENO, " ", 2);
    }
    f_write(STDOUT_FILENO, "\n", 2); //need to write the null terminator
    free_argv(argv);

    // char buf[100]; 
    // f_lseek(STDOUT_FILENO, 0, F_SEEK_SET);
    // f_read(STDOUT_FILENO, buf, 100);
    // printf("This is what i read\n%s\n", buf);
    // printf("pure: ");
    // for(int i = 0; i < 100; i++) {
    //     if(buf[i] != '\0') printf("%c", buf[i]);
    //     else printf("0");
    // }
    p_exit();
}

void shell_touch(char* argv[]) {
    for(int i = 0; argv[i] != NULL; i++) {
        touch(argv[i]);
    }
}

//first arg is source, second is destination
void shell_move(char* argv[]) {
    move(argv[0], argv[1]);
}

void shell_copy(char* argv[]) {
    copy(argv[0], argv[1], 0);
}

void shell_remove(char* argv[]) {
    for(int i = 0; argv[i] != NULL; i++) {
        remove_file(argv[i]);
    }
}

//the first arg is a string that is the new mode (r-x), second arg is 
//the name of the file 
void shell_chmod(char* argv[]) {
    uint8_t new_mode = 0;
    if(argv[0][0] == 'r') new_mode += 4;
    if(argv[0][1] == 'w') new_mode += 2;
    if(argv[0][2] == 'x') new_mode += 1;

    chmod(new_mode, argv[1]);
}

void free_argv(char* argv[]) {
    for(int i = 0; argv[i] != NULL; i++) {
        free(argv[i]);
    }

    free(argv);
}