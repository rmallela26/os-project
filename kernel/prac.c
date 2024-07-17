#include <ucontext.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include "kernel_functions.h"
#include "data_structs.h"
#include "pcb.h"

#define STACKSIZE 40000

// pid_t highest_PID = 0;

// pcb_node* process_table = NULL;

// queue_node* high_queue = NULL;
// queue_node* mid_queue = NULL;
// queue_node* low_queue = NULL;

// queue_node* blocked_queue = NULL;
// queue_node* stopped_queue = NULL;
// queue_node* zombie_queue = NULL;

// pcb* shell_process = NULL;

// ucontext_t* running_process = NULL;

// //switch back to main program after calling hello function

// #include <ucontext.h>
// #include <stdio.h>
// #include <stdlib.h>

// ucontext_t uc1, uc2, sig;

// void f() {
//     running_process = &uc1;
//     printf("Inside function f\n");
//     sleep(1);
//     printf("yayayayayya\n");
//     sleep(1);
//     setcontext(&uc2);
// }

// void g() {
//     running_process = &uc2;
//     printf("Inside function g\n");
//     sleep (1);
//     printf("nonononononono\n");
//     sleep(1);
//     setcontext(&uc1);
// }

// void schedu() {
//     printf("in schedu\n");
//     sleep(2);
//     setcontext(running_process);
// }

// void sig_handler(int signo) {
//     printf("handling signal\n");
//     swapcontext(running_process, &sig);
// }

// int main() {
//     signal(SIGINT, sig_handler);
//     getcontext(&uc1);
//     uc1.uc_stack.ss_sp = malloc(STACKSIZE);
//     uc1.uc_stack.ss_size = STACKSIZE;
//     uc1.uc_stack.ss_flags = 0;
//     // sigemptyset(&(uc.uc_sigmask));

//     getcontext(&uc2);
//     uc2.uc_stack.ss_sp = malloc(STACKSIZE);
//     uc2.uc_stack.ss_size = STACKSIZE;
//     uc2.uc_stack.ss_flags = 0;
//     sigset_t mask;
//     sigemptyset(&mask);
//     sigaddset(&mask, SIGINT);
//     // uc2.uc_sigmask = mask;
//     // uc1.uc_sigmask = mask;

//     getcontext(&sig);
//     sig.uc_stack.ss_sp = malloc(STACKSIZE);
//     sig.uc_stack.ss_size = STACKSIZE;
//     sig.uc_stack.ss_flags = 0;

//     uc1.uc_link = NULL;
//     uc2.uc_link = NULL;

//     makecontext(&uc1, f, 0);
//     makecontext(&uc2, g, 0);
//     makecontext(&sig, schedu, 0);

//     setcontext(&uc1);
    
// }
