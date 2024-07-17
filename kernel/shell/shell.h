#ifndef SHELL 
#define SHELL

#include <sys/types.h>

typedef struct job_node {
    struct job_node* next;
    char* name;
    int job_id;
    int status; //0 is running, 1 is stopped
    pid_t pid;
} job_node;

void shell_main(void);

#endif