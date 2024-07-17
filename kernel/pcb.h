#ifndef PCB_STRUCT
#define PCB_STRUCT

#include <ucontext.h>
#include <sys/types.h>
#include "data_structs.h"
#include <stdbool.h>

typedef struct pcb_node pcb_node;
typedef struct p_fd_table_node p_fd_table_node;

#define READY 0
#define BLOCKED 1
#define STOPPED 2
#define ZOMBIE_NORMAL 3 //terminated normally
#define ZOMBIE_SIGNALED 4 //terminated by signal

#define S_SIGTERM 0
#define S_SIGSTOP 1
#define S_SIGCONT 2

//NOTE: add some way to check if the parent is waiting on this child
//and a pointer to the parent pcb
typedef struct pcb {
    pid_t pid;
    pid_t ppid;
    pcb_node* children;
    int priority; //priority level (-1, 0, 1)
    int status; 
    bool updated; //if the process changes state, change to true until the parent learns of its state change
    bool block_wait; //if being blocking waited on, then set to true;
    int sleep_time;

    ucontext_t execution_state; //holds context of thread
    void* stack_start; //holds the starting point of the stack

    //file descriptor table (inherit from parent)
    p_fd_table_node* fd_table;

} pcb;

#endif