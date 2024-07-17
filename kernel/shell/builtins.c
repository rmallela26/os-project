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

#include "../user_functions.h"
#include "shell.h"
#include "../pcb.h"

extern job_node* jobs;

void print_jobs(void) {
    job_node* ptr = jobs;
    while(ptr != NULL) {
        char* str;
        if(ptr->status == 0) str = "Running";
        else str = "Stopped";
        printf("[%d] %d %s   %s\n", ptr->job_id, ptr->pid, ptr->name, str);
        ptr = ptr->next;
    }
}

void fg(int id) {
    job_node* ptr = jobs;
    while(ptr->job_id != id) ptr = ptr->next;

    //if running, just give it terminal control and wait on it
    //if stopped, send sig continue signal and then give term control and wait

    if(ptr->status == 1) {
        p_kill(ptr->pid, S_SIGCONT);
        ptr->status = 0;
    }

    printf("Running [%d] %s\n", ptr->job_id, ptr->name);
    change_fg(ptr->pid);
    int status;
    p_waitpid(ptr->pid, &status, false);

    //take terminal control back
    change_fg(1); //shell will always be 1

    //check return status, if stopped, update job queue
    if(W_WIFSTOPPED(status)) {
        ptr->status = 1;
    } else {
        //remove job from job queue
        if(jobs == ptr) {
            free(ptr->name);
            jobs = ptr->next;
            free(ptr);
        } else {
            job_node* temp = jobs;
            while(temp->next != ptr) temp = temp->next;
            temp->next = ptr->next;
            free(ptr->name);
            free(ptr);
        }
    }
}

void bg(int id) {
    job_node* ptr = jobs;
    while(ptr->job_id != id) ptr = ptr->next;

    //if running return, if stopped send sig cont
    if(ptr->status == 1) {
        p_kill(ptr->pid, S_SIGCONT);
        ptr->status = 0;
    } else return;

    printf("Running [%d] %s\n", ptr->job_id, ptr->name);
}

void logout(void) {
    exit(EXIT_SUCCESS);
}

//adjust the priority of pid to priority
//assumes process exists
void nice_pid(int priority, pid_t pid) {
    printf("here\n");
    p_nice(pid, priority);
}