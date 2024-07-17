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

#include "kernel_functions.h"
#include "data_structs.h"
#include "pcb.h"
#include "user_functions.h"
#include "fs_system_calls.h"
#include "fat/fat_commands.h"
#include "shell/shell.h"

#define STACKSIZE 40000

//global variables

pcb_node* process_table = NULL;

queue_node* high_queue = NULL;
queue_node* mid_queue = NULL;
queue_node* low_queue = NULL;

queue_node* blocked_queue = NULL;
queue_node* stopped_queue = NULL;
queue_node* zombie_queue = NULL;

pcb_node* sleep_queue = NULL;

pcb* shell_process = NULL;
pcb* running_process = NULL;

os_table_node* main_fd_table = NULL;

int fs_fd;
int fat_size;
int block_size;

uint16_t *fat = NULL;

pcb* foreground_process;

int loggerfd;

ucontext_t scheduler_thread;
ucontext_t handle_termination;

void print_queue(queue_node* queue);

void signal_handler(int signo);

void func() {
    // for(int i = 0; i < 1000; i++) {
    //     if(i == 500) {
    //         p_kill(2, S_SIGSTOP);
    //     } else if (i == 999) {
    //         int status;
    //         p_waitpid(2, &status, false);
    //         if(W_WIFSTOPPED(status)) printf("IT WAS STOPPED\n");
    //         p_kill(2, S_SIGCONT);
    //     }
    //     printf("running func: %d\n", i);
    //     usleep(1000);
    // }
    int status;
    p_waitpid(2, &status, false);
    for(int i = 0; i < 100; i++) {
        // char* str = "done waiting\n";
        // write(STDOUT_FILENO)
        printf("done waiting\n");
        usleep(1000);
    }
}

void ofunc() {
    for(int i = 0; i < 1000; i++) {
        if(i == 800) {
            int status;
            p_waitpid(1, &status, true);
            if(W_WIFEXITED(status)) printf("dead\n");
            p_exit();
            //should be p_exit but also shouldn't segment fault
            //with this figure out what's goign on.
            // k_process_kill(process_table->process, S_SIGTERM, true);
        }
        printf("FOO FOO FOO FOO FOO ---------- %d\n", i);
        usleep(1000);
    }
}

void sfunc() {
    printf("starting p sleep 5 seconds\n");
    p_sleep(50);
    for(int i = 0; i < 100; i++) {
        printf("done sleeping\n");
        usleep(1000);
    }
}

void ffunc() {
    // ls();
    char* name = malloc(10);
    strcpy(name, "foo");
    int fd = f_open(name, F_WRITE);
    // printf("fd is %d\n", fd);

    // off_t offset = find_file_entry("unfoo");
    // lseek(fs_fd, offset, SEEK_SET);
    // char buf[10];
    // read(fs_fd, buf, 10);
    // printf("info bit is %s\n", buf);

    // f_close(fd);
    // f_unlink("foo");

    // ls();
    
    // char* str = "hello world i am an alien";
    // int bwrote = f_write(fd, str, strlen(str) + 1);
    // printf("bwrote is %d\n", bwrote);

    // f_lseek(fd, -3, F_SEEK_CUR);
    // char* st = " fooo";
    // f_write(fd, st, 5);

    // f_lseek(fd, 0, F_SEEK_SET);
    // char buf[100];
    // int bread = f_read(fd, buf, 100);
    // printf("bread is %d, it is %s\n", bread, buf);
    // printf("%s\n", buf+26);
    // sleep(10);
}

int main(void) {
    //mount filesystem
    char* fs_name = "fat/fat_fs";
    // mkfs(fs_name, 32, 1);
    mount(fs_name);

    signal(SIGALRM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGTSTP, signal_handler);

    loggerfd = open("logs.txt", O_CREAT | O_APPEND | O_RDWR);

    pcb* root = malloc(sizeof(pcb));
    root->pid = 0;
    root->ppid = -1;
    root->children = NULL;
    
    p_fd_table_node* node1 = malloc(sizeof(p_fd_table_node));
    p_fd_table_node* node2 = malloc(sizeof(p_fd_table_node));
    p_fd_table_node* node3 = malloc(sizeof(p_fd_table_node));

    node1->fd = STDIN_FILENO;
    node1->mode = 0;
    node1->offset = 0;
    node1->main_entry_ptr = NULL;
    node1->next = node2;

    node2->fd = STDOUT_FILENO;
    node2->mode = 0;
    node2->offset = 0;
    node2->main_entry_ptr = NULL;
    node2->next = node3;

    node3->fd = STDERR_FILENO;
    node3->mode = 0;
    node3->offset = 0;
    node3->main_entry_ptr = NULL;
    node3->next = NULL;

    root->fd_table = node1;

    char* argv[1];
    void (*func1)() = shell_main;
    
    pcb* child = k_process_create(root, func1, argv, -1);
    // p_nice(child->pid, -1); //make priority high for shell
    change_fg(1);
    shell_process = child;

    //set random seed for the scheduler 
    struct timeval time;
    gettimeofday(&time, NULL);
    srand((time.tv_sec * 1000) + (time.tv_usec / 1000));
    
    //setup kernel threads
    //scheduler_thread schedules and executes the next process
    //handle_termination thread handles natural terminations of threads
    getcontext(&scheduler_thread);
    scheduler_thread.uc_stack.ss_sp = malloc(STACKSIZE);
    scheduler_thread.uc_stack.ss_size = STACKSIZE;

    scheduler_thread.uc_stack.ss_flags = 0;
    scheduler_thread.uc_link = NULL;
    makecontext(&scheduler_thread, schedule, 0);

    getcontext(&handle_termination);
    handle_termination.uc_stack.ss_sp = malloc(STACKSIZE);
    handle_termination.uc_stack.ss_size = STACKSIZE;

    handle_termination.uc_stack.ss_flags = 0;
    handle_termination.uc_link = NULL;
    makecontext(&handle_termination, termination_handler, 0);

    
    //setup the timer
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 100000; // 100 milliseconds
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 100000; // 100 milliseconds

    // Start a real-time timer
    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("setitimer");
        exit(1);
    }

    // pause();
    
    sigset_t mask;
    sigemptyset(&mask);
    // sigaddset(&mask, SIGALRM);
    sigsuspend(&mask);
}

void print_queue(queue_node* queue) {
    while(queue != NULL) {
        // printf("in mid print\n");
        printf("%d, ", queue->process->pid);
        // if(queue->process->pid == 3432) exit(0);
        queue = queue->prev;
        // printf("que next null? %d\n", queue == NULL);
    }
    printf("\n");
}

void signal_handler(int signo) {
    /*
    SIGALRM handler is invoked on every clock tick. Every clock tick it
    calls the scheduler thread to pick the next process to run and run it. 
    If there was a previously running process (i.e. the idle process wasn't
    running), put it back on the the right queue, save the execution context,
    and call the scheduler thread. 
    */
    if(signo == SIGALRM) {
        // printf("Process table: \n");
        // printf("Process %d has priority %d\n", process_table->process->pid, process_table->process->priority);
        // if(process_table->next != NULL) printf("Process %d has priority %d\n", process_table->next->process->pid, process_table->next->process->priority);
        
        // printf("Main fd table: \n");
        // if(main_fd_table != NULL) printf("File 1: %s", main_fd_table->name);
        //clock tick

        //update the sleep queue
        pcb_node* ptr = sleep_queue;
        pcb_node* prev = NULL;

        while(ptr != NULL) {
            ptr->process->sleep_time--;
            // printf("sleep time is %d\n", ptr->process->sleep_time);
            //if done sleeping and blocked, unblock (can be done sleeping but stopped)
            if(ptr->process->sleep_time == 0 && ptr->process->status == BLOCKED) {
                blocked_queue = remove_process(ptr->process, blocked_queue);

                //add the process back to the ready queues
                if(ptr->process->priority == -1) high_queue = enqueue(ptr->process, high_queue);
                else if(ptr->process->priority == 0) mid_queue = enqueue(ptr->process, mid_queue);
                else low_queue = enqueue(ptr->process, low_queue);

                //remove node from sleep queue
                if(prev == NULL) {
                    sleep_queue = ptr->next;
                    free(ptr);
                    ptr = sleep_queue;
                }
                else {
                    prev->next = ptr->next;
                    pcb_node* temp = ptr->next;
                    free(ptr);
                    ptr = temp;
                }
            } else if(ptr->process->sleep_time == 0 || ptr->process->status == ZOMBIE_SIGNALED) {
                //either process is done sleeping but is stopped or 
                //process was killed when it was sleeping
                //so just remove from sleep queue

                //remove from sleep queue
                if(prev == NULL) {
                    sleep_queue = ptr->next;
                    free(ptr);
                    ptr = sleep_queue;
                }
                else {
                    prev->next = ptr->next;
                    pcb_node* temp = ptr->next;
                    free(ptr);
                    ptr = temp;
                }
            } else {
                prev = ptr;
                ptr = ptr->next;
            }
            
        }

        //DEBUG
        //print all queues
        // printf("High: ");
        // print_queue(high_queue);
        // printf("Mid: ");
        // print_queue(mid_queue);
        // printf("Low: ");
        // print_queue(low_queue);
        // printf("Stopped: ");
        // print_queue(stopped_queue);
        // printf("Blocked: ");
        // print_queue(blocked_queue);
        // printf("Zombie: ");
        // print_queue(zombie_queue);

        // if(running_process != NULL) printf("Running process was: %d\n", running_process->pid);
        // else printf("Running process was idle process\n");

        //put the thread back on the ready queue
        // printf("clock tick\n");
        if(running_process == NULL) {
            //was running the idle process
            setcontext(&scheduler_thread);
        } else if(running_process->status == ZOMBIE_NORMAL) return; //handle_termination thread was running, and it will call scheduler from there

        if(running_process->priority == -1) high_queue = enqueue(running_process, high_queue);
        else if(running_process->priority == 0) mid_queue = enqueue(running_process, mid_queue);
        else low_queue = enqueue(running_process, low_queue);
        // printf("Mid again: ");
        // print_queue(mid_queue);
        swapcontext(&(running_process->execution_state), &scheduler_thread);
    } else if(signo == SIGINT) {
        //check if current process is shell, if it is ignore
        //otherwise send sigterm to the running process
        if(foreground_process != shell_process) {
            k_process_kill(foreground_process, S_SIGTERM, true);
            setcontext(&scheduler_thread);
        }
        // setcontext(&scheduler_thread);
    } else if(signo == SIGTSTP) {
        if(foreground_process != shell_process) {
            k_process_kill(foreground_process, S_SIGSTOP, true);
            setcontext(&scheduler_thread);
        }
        // setcontext(&scheduler_thread);
    }
}