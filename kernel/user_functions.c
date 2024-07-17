#include <ucontext.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "pcb.h"
#include "data_structs.h"
#include "kernel_functions.h"
#include "user_functions.h"
#include "fs_system_calls.h"

//global variables

extern pcb_node* process_table;

extern queue_node* high_queue;
extern queue_node* mid_queue;
extern queue_node* low_queue;

extern queue_node* blocked_queue;
extern queue_node* stopped_queue;
extern queue_node* zombie_queue;

extern pcb_node* sleep_queue;

extern pcb* shell_process;
extern pcb* running_process;

extern pcb* foreground_process;

extern ucontext_t scheduler_thread;
extern ucontext_t handle_termination;

/*
User level function. Function forks a new thread, and once it is spawned,
executes a function. 
Arguments:
    func: A pointer to the function to execute once the thread is spawned
    argv: The array of arguments to the function func
    fd0: The input fd. It is -1 if there is no redirection
    fd1: The output fd. It is -1 if there is no redirection
*/
pid_t p_spawn(void (*func)(), char *argv[], int fd0, int fd1) {
    //set default priority to 0
    pcb* child = k_process_create(running_process, func, argv, 0);
    // printf("here\n");
    //initialize redirections
    if(fd0 != -1) duplicate(STDIN_FILENO, fd0, child);
    if(fd1 != -1) duplicate(STDOUT_FILENO, fd1, child);

    return child->pid;
}

pid_t p_spawn_nice(void (*func)(), char *argv[], int fd0, int fd1, int priority) {
    pcb* child = k_process_create(running_process, func, argv, priority);
    // printf("here\n");
    //initialize redirections
    if(fd0 != -1) duplicate(STDIN_FILENO, fd0, child);
    if(fd1 != -1) duplicate(STDOUT_FILENO, fd1, child);

    return child->pid;
}

/*
User level function. Function sets calling process as blocked if 
nohang is false and calls the scheduler to schedule a different thread
to run. If nohang is true, the function returns immediately. The function
sets wstatus to the appropriate value. 
Arguments:
    pid: The pid of the process that is being waited on 
    wstatus: A pointer to the place where the status will be stored
    nohang: True if nonblocking wait, otherwise false
Returns:
    The pid of the process if the child changed status, otherwise -1
*/
pid_t p_waitpid(pid_t pid, int *wstatus, bool nohang) {
    //find the process in the process table
    pcb_node* ptr = process_table;
    while(ptr->process->pid != pid) ptr = ptr->next;

    if(!ptr->process->updated) {
        if(!nohang) {
            //process is running, so won't be on one of the ready queues
            // if(running_process->priority == -1) high_queue = remove_process(running_process, high_queue);
            // else if(running_process->priority == 0) mid_queue = remove_process(running_process, mid_queue);
            // else low_queue = remove_process(running_process, low_queue);
            
            blocked_queue = enqueue(running_process, blocked_queue);
            ptr->process->block_wait = true;
            running_process->status = BLOCKED;
            //Not setting updated, check_parent_waiting, doesn't make sense

            //schedule the next thread 
            swapcontext(&(running_process->execution_state), &scheduler_thread);
        }
    }
    //IF TERMINATED THEN CLEAN UP THE PCB
    //check for change in status, set the status, and return pid
    
    if(ptr->process->updated) {
        ptr->process->updated = false;
        *wstatus = ptr->process->status;
        
        if(ptr->process->status == ZOMBIE_NORMAL || ptr->process->status == ZOMBIE_SIGNALED) {
            //clean up
            k_process_cleanup(ptr->process);
        }

        return pid;
    } else return -1;
}

/*
User level function. Sends a signal to a process. 
Arguments:
    pid: The pid of the process that the signal is sent to
    signal: The signal to be sent
Returns:
    0 on success, -1 on error
*/
int p_kill(pid_t pid, int sig) {
    if(sig != S_SIGSTOP && sig != S_SIGCONT && sig != S_SIGTERM) return -1;

    //find the pcb of the process
    pcb_node* ptr = process_table;
    while(ptr->process->pid != pid) ptr = ptr->next;

    return k_process_kill(ptr->process, sig, true);
}

/*
User level function. It exits the current thread unconditionally by 
swapping to the handle_termination thread
*/
void p_exit(void) {
    //just call the termination thread because this is a normal exit
    //since it killed itself. The termination handler already takes care
    //of parent waiting and killing children, etc. 
    swapcontext(&(running_process->execution_state), &handle_termination);
}

/*
User level function. It sets the priority of of a process to a specified
priority. 
Arguments:
    pid: The pid of the process whose priority will change
    priority: The priority the process will change to
Returns:
    0 on success, -1 if the priority is already the priority specified
*/
int p_nice(pid_t pid, int priority) {
    pcb_node* ptr = process_table;
    while(ptr->process->pid != pid) ptr = ptr->next;

    pcb* process = ptr->process;

    if(process->priority == priority) return -1;
    
    //if process is blocked don't try to remove 
    if(process->status != BLOCKED && process->status != STOPPED) {
        if(process->priority == -1) high_queue = remove_process(process, high_queue);
        else if(process->priority == 0) mid_queue = remove_process(process, mid_queue);
        else low_queue = remove_process(process, low_queue);
    }

    process->priority = priority;

    if(process->status != BLOCKED && process->status != STOPPED) { //processes that are sleeping or blocked or stopped should stay like that
        if(priority == -1) high_queue = enqueue(process, high_queue);
        else if(priority == 0) {mid_queue = enqueue(process, mid_queue); printf("somehow here\n");}
        else {low_queue = enqueue(process, low_queue); printf("over here\n");}
    }
    return 0;
}

/*
User level function. Sets the calling process to sleep for a specified
number of clock ticks. It is the job of the clock tick cycle in the sigalarm
handler to set the thread to running when the clock ticks are done. 
*/
void p_sleep(unsigned int ticks) {
    //add process to sleep queue
    pcb_node* s = malloc(sizeof(pcb_node));
    running_process->sleep_time = ticks;
    s->next = sleep_queue;
    s->process = running_process;
    sleep_queue = s;

    //process isn't going to be on a queue since it is running right now
    // if(running_process->priority == -1) high_queue = remove_process(running_process, high_queue);
    // else if(running_process->priority == 0) mid_queue = remove_process(running_process, mid_queue);
    // else low_queue = remove_process(running_process, low_queue);
    running_process->status = BLOCKED;
    //not setting updated as true because doesn't make sense
    blocked_queue = enqueue(running_process, blocked_queue);
    swapcontext(&(running_process->execution_state), &scheduler_thread);
}

/*
Change what thread has "terminal control". It does this by changing
the foreground_process global pointer. 
Arguments:
    pid: The pid of the process that the foreground should go to. If pid
    is -1, that means change the foreground to the shell process. 
*/
void change_fg(pid_t pid) {
    pcb_node* ptr = process_table;
    if(pid == -1) {
        foreground_process = shell_process; 
    } else {
        while(ptr->process->pid != pid) ptr = ptr->next;
        foreground_process = ptr->process;
    }
}

void list_processes(void) {
    pcb_node* ptr = process_table;
    char* str = "PID\tPPID\tPRIORITY\n";
    // printf("size of str is %d\n", strlen(str));
    f_write(STDOUT_FILENO, str, strlen(str)+1);
    int buffer_size = snprintf(NULL, 0, "%d\t%d\t%d\n", 1, 2, -1);
    while(ptr != NULL) {
        char* formatted_string = malloc(buffer_size+1);
        sprintf(formatted_string, "%d\t%d\t%d\n", ptr->process->pid, ptr->process->ppid, ptr->process->priority);
        f_write(STDOUT_FILENO, formatted_string, buffer_size);

        ptr = ptr->next;
    }
}

/*
The following three functions are user level functions that 
are called through the macros W_WIFEXITED, W_WIFSTOPPED, and
W_WIFSIGNALED. They return true if the child terminated normally,
if the child is stopped, or if the child terminated by a signal, 
respectively. 
Arugments:
    status: The status of the process to be checked. 
Returns:
    True or false as indicated above.
*/
bool check_exited(int status) {
    if(status == ZOMBIE_NORMAL) return true;
    else return false;
}

bool check_stopped(int status) {
    if(status == STOPPED) return true;
    else return false;
}

bool check_signaled(int status) {
    if(status == ZOMBIE_SIGNALED) return true; 
    else return false;
}