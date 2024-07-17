#include <ucontext.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include "pcb.h"
#include "data_structs.h"
#include "fs_system_calls.h"
// #include "shell/shell.h"
#include "user_functions.h"

#define STACKSIZE 40000

//global variables

extern pcb_node* process_table;

extern queue_node* high_queue;
extern queue_node* mid_queue;
extern queue_node* low_queue;

extern queue_node* blocked_queue;
extern queue_node* stopped_queue;
extern queue_node* zombie_queue;

extern pcb_node* sleep_queue;

extern pcb* running_process;

extern ucontext_t scheduler_thread;
extern ucontext_t handle_termination;

extern int loggerfd;

void kill_children(pcb* process);
void k_process_cleanup(pcb* process);
void check_parent_waiting(pcb* process);
void dec_main_fd_table(pcb* process);

//clock tick thread
//gets called every clock tick
//stops current process execution, handles finished processes
//calls scheduler function to schedule the next function

//scheduler function
//schedules next process and runs it

//cleanup thread
//this is the thread that every process thread links to
//it is called when a process terminates 
//handle appropriate queues and calls scheduler function

//

/*
Kernel level function. Function to create a new child thread and add it 
to the ready queue. Creates the PCB of the child and adds it to the 
process table. Also adds process to the ready queue. Inherits file descriptor 
table from parent. 
NOTE: Need to initialize execution_state where appropriate. 
NEED TO HANDLE STDIN_FILENO, STDOUT, STDERR PROPERLY EVERYWHERE (shouldn't
be freeing them or trying to update in main table somewhere)
Arguments: 
    parent: Pointer to the parent's PCB
    func: A function pointer to the function that the child thread is supposed to execute
    argv: The arguments to func
Returns:
    A pointer to the newly created child's PCB
*/
pcb* k_process_create(pcb *parent, void (*func)(), char *argv[], int priority)  {
    pcb* childPCB = malloc(sizeof(pcb));
    //set pid to one more than the current highest one
    if(process_table != NULL) childPCB->pid = process_table->process->pid + 1;
    else childPCB->pid = 1;
    childPCB->ppid = parent->pid;

    //Set new child node to head of children list since order doesn't 
    //matter. Add the node to the parent's children list. 
    pcb_node* c_node = malloc(sizeof(pcb_node));
    c_node->next = parent->children;
    c_node->process = childPCB;
    parent->children = c_node;

    childPCB->children = NULL;
    childPCB->updated = false;

    childPCB->priority = priority;
    
    childPCB->status = READY;
    childPCB->fd_table = NULL; //so that the last node's next is NULL

    childPCB->sleep_time = 0;
    
    //inherit file descriptor table and add 1 for each file opened by child
    //in the main fd table

    p_fd_table_node* fd_ptr = parent->fd_table;

    while(fd_ptr != NULL) {
        p_fd_table_node* child_head = malloc(sizeof(p_fd_table_node));
        // printf("fd ptr fd: %d\n", fd_ptr->fd);
        // sleep(1);
        // if(fd_ptr->main_entry_ptr != NULL) printf("opening file %s\n", fd_ptr->main_entry_ptr->name);
        child_head->main_entry_ptr = fd_ptr->main_entry_ptr;
        if(child_head->main_entry_ptr != NULL) child_head->main_entry_ptr->num_open++;
        
        child_head->fd = fd_ptr->fd;
        child_head->mode = fd_ptr->mode;
        child_head->offset = fd_ptr->offset;

        //set next node to current head and head to this node since order
        //doesn't matter
        child_head->next = childPCB->fd_table;
        childPCB->fd_table = child_head;

        fd_ptr = fd_ptr->next;
    }

    //set up the execution context
    getcontext(&(childPCB->execution_state));
    childPCB->execution_state.uc_stack.ss_sp = malloc(STACKSIZE);
    childPCB->stack_start = childPCB->execution_state.uc_stack.ss_sp;
    childPCB->execution_state.uc_stack.ss_size = STACKSIZE;

    childPCB->execution_state.uc_stack.ss_flags = 0;
    childPCB->execution_state.uc_link = &handle_termination;
    makecontext(&(childPCB->execution_state), func, 1, argv);

    pcb_node* pt_node = malloc(sizeof(pcb_node));
    pt_node->next = process_table;
    pt_node->process = childPCB;
    process_table = pt_node;

    mid_queue = enqueue(childPCB, mid_queue);
    return childPCB;
}

/*
Kernel level function. Sends a signal to a process. Does this by either 
terminating the process or changing its status and its queue. 
NOTE: should call some function to check if parent is waiting on child,
and if it is, change parent to ready. 
Arguments:
    process: Poitner to the PCB of the process to be signaled
    signal: The signal to be delivered
    add_zombie: A flag for if the the process should be added 
            to the zombie queue (used by kill_children())
Returns:
    0 on success, -1 on error
*/
int k_process_kill(pcb* process, int signal, bool add_zombie) {
    if(signal == S_SIGTERM) {
        //terminate the process
        bool flag = false; //keep track of if we found the process

        //find what queue it is in and remove it
        if(process == running_process) flag = true;
        else if(process->status == READY) {
            if(process->priority == -1) {
                high_queue = remove_process(process, high_queue);
                flag = true;
            }
            else if(process->priority == 0) {
                flag = true;
                mid_queue = remove_process(process, mid_queue);
            }
            else if(process->priority == 1) {
                flag = true;
                low_queue = remove_process(process, low_queue);
            }
        } else if(process->status == BLOCKED) {
            flag = true;
            blocked_queue = remove_process(process, blocked_queue);
        } else if(process->status == STOPPED) {
            flag = true;
            stopped_queue = remove_process(process, stopped_queue);
        } else if(process->status == ZOMBIE_NORMAL || process->status == ZOMBIE_SIGNALED) {
            //this is for when kill_children is called, if some child
            //is already terminated it will be on the zombie queue,
            //so it needs to be taken off. 
            zombie_queue = remove_process(process, zombie_queue);
        } 

        if(add_zombie && !flag) return -1; //didn't find process
        dec_main_fd_table(process);
        kill_children(process);
        if(!add_zombie) return 0;

        process->status = ZOMBIE_SIGNALED;
        process->updated = true;

        check_parent_waiting(process);
        //add process to zombie queue
        zombie_queue = enqueue(process, zombie_queue);
        return 0;
    } else if(signal == S_SIGSTOP) {
        //stop the process
        bool flag = false;
        //find what queue it is on and move it to the stopped queue
        if(process->status == READY) {
            if(process->priority == -1) {
                high_queue = remove_process(process, high_queue);
                flag = true;
            }
            else if(process->priority == 0) {
                flag = true;
                mid_queue = remove_process(process, mid_queue);
            }
            else if(process->priority == 1) {
                flag = true;
                low_queue = remove_process(process, low_queue);
            }
        } else if(process->status == BLOCKED) {
            flag = true;
            blocked_queue = remove_process(process, blocked_queue);
        }

        if(!flag) return -1;
        process->updated = true;
        check_parent_waiting(process);
        process->status = STOPPED;
        stopped_queue = enqueue(process, stopped_queue);
        return 0;
    } else if(signal == S_SIGCONT) {
        //continue a stopped process
        //find if it is on the stopped queue and move it to the ready queue
        bool flag = false;
        if(process->status == STOPPED) {
            flag = true;
            stopped_queue = remove_process(process, stopped_queue);
        }

        if(!flag) return -1;
        process->updated = false; //since we are continuing, set back to false
                                //should only be true if soemone waited on the process
                                //while it was stopped
        if(process->sleep_time > 0) { //check if still needs to sleep
            process->status = BLOCKED;
            blocked_queue = enqueue(process, blocked_queue);
            return 0;
        }
        //not doing check_parent_waiting, doesn't make sense
        process->status = READY;
        if(process->priority == 1) low_queue = enqueue(process, low_queue);
        else if(process->priority == 0) mid_queue = enqueue(process, mid_queue);
        else high_queue = enqueue(process, high_queue);
        return 0;
    }
    return -1;
}

/*
Kernel level function. Function to kill all children of a process.
Kills the child by finding its queue and removing it. Doesn't
enqueue it on zombie_queue because the process is being removed too. 
After moving it, calls cleanup on the child.
Arguments:
    process: A pointer to the PCB of the process whose children need
            to be killed
    Returns:
        Nothing
*/
void kill_children(pcb* process) {
    pcb_node* ptr = process->children;
    while(ptr != NULL) {
        k_process_kill(ptr->process, S_SIGTERM, false);
        k_process_cleanup(ptr->process);
        ptr = ptr->next;
    }
}

/*
Kernel level function. It is called when a process dies, either from
k_process_kill or from the handle_termination thread. It runs through
the processes' file descriptor table and updates the main fd table
by removing 1 from the count for each open file. 
Arguments:
    process: The process that has died
Returns:
    Nothing
*/
void dec_main_fd_table(pcb* process) {
    p_fd_table_node* ptr = process->fd_table;
    while(ptr != NULL) {
        if(ptr->main_entry_ptr != NULL) {
            // printf("closing file %s\n", ptr->main_entry_ptr->name); 
            f_close(ptr->fd);
        }
        // if(ptr->main_entry_ptr != NULL) ptr->main_entry_ptr->num_open--;
        ptr = ptr->next;
    }
}

/*
Kernel level function. Cleans up the PCB and removes the process 
from the process table. This is called once a process has been 
waited on, or if a parent dies. 
Arguments:
    process: A pointer to the PCB of the process to be cleaned up
Returns:
    Nothing
*/
void k_process_cleanup(pcb* process) {
    //remove process from process table
    pcb_node* ptr = process_table;
    if(process_table->process == process) {
        process_table = process_table->next;
        free(ptr);
    } else {
        while((ptr->next)->process != process) {
            ptr = ptr->next;
        }
        pcb_node* temp = ptr->next;
        ptr->next = (ptr->next)->next;
        free(temp);
    }

    //kill all children
    // kill_children(process);
    
    //remove process from zombie queue
    //all its children would have been immediately killed and cleaned up
    //without being added to the zombie queue, but this process is still on
    //zombie queue and needs to be removed
    zombie_queue = remove_process(process, zombie_queue);

    //remove process from its parents' list of children and free that pcb node
    ptr = process_table;

    // printf("process table is null? %d next is null? %d\n", ptr->process->pid, ptr->next->process->pid);
    while(ptr->process->pid != process->ppid) ptr = ptr->next;
    pcb_node* child_ptr = ptr->process->children;
    if(child_ptr->process == process) {
        ptr->process->children = ptr->process->children->next;
        free(child_ptr);
    } else {
        while(child_ptr->next->process != process) child_ptr = child_ptr->next;
        pcb_node* temp = child_ptr->next;
        child_ptr->next = (child_ptr->next)->next;
        free(temp);
    }

    //remove from sleep queue if needed
    if(process->sleep_time > 0) {
        // printf("process pid is %d and sleep time is %d\n", process->pid, process->sleep_time);
        pcb_node* sleep_ptr = sleep_queue;
        pcb_node* prev = NULL;
        while(sleep_ptr->process != process) {
            prev = sleep_ptr;
            sleep_ptr = sleep_ptr->next;
        }

        if(prev == NULL) {
            sleep_queue = sleep_ptr->next;
            free(sleep_ptr);
        } else {
            prev->next = sleep_ptr->next;
            free(sleep_ptr);
        }
    }

    //clean up PCB
    free(process->stack_start);
    ptr = process->children;
    while(ptr != NULL) {
        pcb_node* next = ptr->next;
        free(ptr);
        ptr = next;
    }

    //cleanup the file descriptor table, main_fd table will already be
    //updated by this point
    p_fd_table_node* fd_ptr = process->fd_table;
    while(fd_ptr != NULL) {
        p_fd_table_node* temp = fd_ptr->next;
        free(fd_ptr);
        fd_ptr = temp;
    }


    free(process);
}

/*
Kernel level function. Function is called when the scheduler thread 
gets invoked. It schedules the next thread to run. The scheduler thread
is invoked every clock tick or if some other thread calls it (e.g. the 
cleanup thread, system call). If there are no threads to run it schedules 
the idle process.
Arguments:
    None
Returns:
    Nothing
*/
void schedule(void) {
    
    // Generate a random number between 0 and 1
    double random_number;

    bool low = true;
    bool mid = true;
    bool high = true;
    
    pcb* process = NULL;

    while(low || mid || high) {
        random_number = (double)random() / RAND_MAX;
        if(random_number < 0.21) {
            if(!low) continue;
            low = false;
            if((process = pop(&low_queue)) != NULL) break;
        } else if(random_number < 0.21*1.5 + 0.21) {
            if(!mid) continue;
            mid = false;
            if((process = pop(&mid_queue)) != NULL) break;
        } else {
            if(!high) continue;
            high = false;
            if((process = pop(&high_queue)) != NULL) break;
        }
    }
    
    bool idle = process == NULL;

    if(!idle) {
        // char buf[2];
        // if(process->pid == 1) buf[0] = 'a';
        // else buf[0] = 'b';
        // // buf[0] = process->pid;
        // buf[1] = '\n';
        // write(loggerfd, buf, 2);
        // if(process->pid == 2) printf("%d\n", process);
        running_process = process;
        setcontext(&(process->execution_state));
    } else {
        //run the idle process
        running_process = NULL;
        sigset_t mask;
        sigemptyset(&mask);
        sigsuspend(&mask);
    }
}

/*
Handle the termination of a process. This function is called by the
handle_termination thread. This is the thread that all child threads
link to, so is called whenever a child thread terminates naturally. 
It changes the status of the process and adds it to the zombie queue. 
Arguments:
    None
Returns:
    Nothing
*/
void termination_handler(void) {
    // printf("IN TERMINATION HANDLER\n");
    running_process->status = ZOMBIE_NORMAL;
    zombie_queue = enqueue(running_process, zombie_queue);
    
    dec_main_fd_table(running_process);
    kill_children(running_process);

    running_process->updated = true;
    
    check_parent_waiting(running_process);
    setcontext(&scheduler_thread);
}

void check_parent_waiting(pcb* process) {
    if(process->block_wait) {
        //find and put the parent back on the ready queue
        pcb_node* ptr = process_table;
        while(ptr->process->pid != process->ppid) ptr = ptr->next;

        blocked_queue = remove_process(ptr->process, blocked_queue);

        if(ptr->process->priority == -1) high_queue = enqueue(ptr->process, high_queue);
        else if(ptr->process->priority == 0) mid_queue = enqueue(ptr->process, mid_queue);
        else low_queue = enqueue(ptr->process, low_queue);
    }
}