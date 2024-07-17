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

void free_argv2(char* argv[]);

//sleep for n seconds
void shell_sleep(char* argv[]) {
    int n = atoi(argv[0]);
    p_sleep(n*10); //10n clock ticks is n seconds
    free_argv2(argv);
    p_exit();
}

void ps(void) {
    list_processes();
}

//first arg is signal name (-term, -stop, -cont)
//remaining args are pids to send to 
void shell_kill(char* argv[]) {
    int signal = 0;
    if(strcmp(argv[0], "-term") == 0) signal = S_SIGTERM;
    else if(strcmp(argv[1], "-stop") == 0) signal = S_SIGSTOP;
    else signal = S_SIGCONT;

    for(int i = 1; argv[i] != NULL; i++) {
        p_kill(atoi(argv[i]), signal);
    }
}

void free_argv2(char* argv[]) {
    for(int i = 0; argv[i] != NULL; i++) {
        free(argv[i]);
    }

    free(argv);
}