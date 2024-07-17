#ifndef DATA_STRUCTS
#define DATA_STRUCTS

#include "pcb.h"
#include <stdbool.h>

typedef struct pcb pcb;

typedef struct queue_node {
    pcb* process;
    struct queue_node* prev;
} queue_node;

typedef struct pcb_node {
    struct pcb_node* next;
    pcb* process;
} pcb_node;

typedef struct os_table_node {
    int fd;
    int num_open; //how many processes have this file open
    int permissions; //one process can change it, but needs to apply to all processes
    char* name; //one process can change it, but needs to apply to all processes
    bool write_mode; //false if no process has it opened in write mode, switched to true if 1 does
                    //only one process can have it in write mode so checks this var
    off_t fentry_offset; //the offset to the file entry in the directory block
    struct os_table_node* next;
} os_table_node;

typedef struct p_fd_table_node {
    struct os_table_node* main_entry_ptr; //will be null if fd <= 2 AND no redirection 
    int fd; //if it's <= 2, then it is stdin, out, or err
    int mode; //write or append
    off_t offset; //current offset in the file
    struct p_fd_table_node* next;
} p_fd_table_node;

queue_node* enqueue(pcb* process, queue_node* head);
pcb* pop(queue_node** head);
queue_node* remove_process(pcb* process, queue_node* head);

#endif