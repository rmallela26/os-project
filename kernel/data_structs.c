#include <ucontext.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "pcb.h"

/*
Add a process to the back of a queue. 
Arguments: 
    process: The process to be added
    head: The head of the queue process should be added to
Returns:
    Nothing
*/
queue_node* enqueue(pcb* process, queue_node* head) {
    if(head == NULL) {
        queue_node* q_node = malloc(sizeof(queue_node));
        if(q_node == NULL) {
            perror("no memory");
            return head;
        }

        q_node->prev = NULL;
        q_node->process = process;
        head = q_node;
        // printf("in q node process pid: %d\n", process->pid);
        return head;
    }
    queue_node* ptr = head;
    while(ptr->prev != NULL) ptr = ptr->prev;
    queue_node* q_node = malloc(sizeof(queue_node));

    q_node->prev = NULL;
    q_node->process = process;
    // printf("in q node process pid: %d\n", process->pid);

    ptr->prev = q_node;
    return head;
}

/*
Pop a process from the front of the queue. 
Arguments:
    head: Pointer to pointer to the head of the queue that the 
        process should be popped from 
Returns: 
    A pointer to the the PCB of the process that was popped
*/
pcb* pop(queue_node** head) {
    if(*head == NULL) return NULL;
    pcb* process = (*head)->process;
    queue_node* ptr = *head;
    *head = (*head)->prev;
    free(ptr);
    return process;
}

/*
Find a process and remove it from the queue
Arguments:
    process: Pointer to the process that should be removed
    head: Pointer to the head of the queue that the process is in
Returns:
    The head of the queue
*/
queue_node* remove_process(pcb* process, queue_node* head) {
    if(head == NULL) return head;
    if(head->process == process) {
        head = head->prev;
        return head;
    }

    queue_node* ptr = head;
    while((ptr->prev)->process != process) ptr = ptr->prev;
    queue_node* temp = (ptr->prev)->prev;
    free(ptr->prev);
    ptr->prev = temp;
    return head;
}