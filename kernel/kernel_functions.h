#ifndef KERNEL_FUNCTIONS
#define KERNEL_FUNCTIONS

#include "pcb.h"
#include <stdbool.h>

pcb* k_process_create(pcb *parent, void (*func)(), char *argv[], int priority);
int k_process_kill(pcb* process, int signal, bool add_zombie);
void k_process_cleanup(pcb* process);
void kill_children(pcb* process);
void schedule(void);
void termination_handler(void);

#endif