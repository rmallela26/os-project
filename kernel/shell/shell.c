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

#include "parser.h"
#include "../fs_system_calls.h"
#include "../user_functions.h"
#include "fs_functions.h"
#include "os_functions.h"
#include "shell.h"
#include "builtins.h"

job_node* jobs = NULL;

job_node* enqueue_job(pid_t pid, char* job_name, int stat);
void wait_children(void);

//this is the process that gets spawned when the os starts up
void shell_main(void) {
    while(1) {
        struct parsed_command *cmd;

        char* command = NULL;
        size_t size = 0;
        printf("%s", PROMPT);
        int num_bytes = getline(&command, &size, stdin);

        if(num_bytes == -1) {
            //shutdown the os and shell
            exit(EXIT_SUCCESS);
        }

        int i = parse_command(command, &cmd);

        if (i < 0) {
            perror("parse_command");
            exit(EXIT_FAILURE);
        }
        if (i > 0) {
            printf("parser error: %d\n", i);
            exit(EXIT_FAILURE);
        }

        if(strcmp(command, "\n") == 0) {
            wait_children();
            continue;
        }

        bool nice = false;
        int priority = 0;

        //check if command is a builtin
        if(strcmp(cmd->commands[0][0], "jobs") == 0) {
            print_jobs();
            wait_children();
            continue;
        }
        else if(strcmp(cmd->commands[0][0], "fg") == 0) {
            fg(atoi(cmd->commands[0][1]));
            wait_children();
            continue;
        }
        else if(strcmp(cmd->commands[0][0], "bg") == 0) {
            bg(atoi(cmd->commands[0][1]));
            wait_children();
            continue;
        } else if(strcmp(cmd->commands[0][0], "nice_pid") == 0) {
            nice_pid(atoi(cmd->commands[0][1]), atoi(cmd->commands[0][2]));
            wait_children();
            continue;
        } else if(strcmp(cmd->commands[0][0], "nice") == 0) {
            nice = true;
            priority = atoi(cmd->commands[0][1]);
            cmd->commands[0] = cmd->commands[0]+2;

            // printf("command name %s command arg %s\n", cmd->commands[0][0], cmd->commands[0][1]);
            // logout();
        }
        else if(strcmp(cmd->commands[0][0], "logout") == 0) {
            logout();
        }

        // //add job to jobs list
        char* job_name = cmd->commands[0][0];
        // strcpy(job_name, cmd->commands[0][0]);

        // job_node* new_job = malloc(sizeof(job_node));
        // new_job->name = job_name;
        // new_job->next = NULL;
        
        // if(jobs == NULL) {
        //     new_job->job_id = 1;
        //     jobs = new_job;
        // } else {
        //     job_node* ptr = jobs;
        //     while(ptr->next != NULL) ptr = ptr->next;
        //     new_job->job_id = ptr->job_id+1;
        //     ptr->next = new_job;
        // }

        void (*func)() = NULL;
        char** argv = NULL;

        //spawn the appropriate process
        if(strcmp(job_name, "ls") == 0) {
            func = shell_ls;
        } else if(strcmp(job_name, "cat") == 0) {
            func = cat;

            int num = 0;
            while(cmd->commands[0][num] != NULL) num++;
            num--; //remove the command name

            // printf("num is %d\n", num);
            argv = malloc((num+3)*sizeof(char*)); 
            for(int i = 1; i <= num; i++) {
                char* str = malloc(sizeof(cmd->commands[0][i]));
                strcpy(str, cmd->commands[0][i]);
                argv[i] = str;
                // printf("str is %s\n", str);
            }

            char* str = malloc(sizeof(char));
            if(cmd->is_file_append) strcpy(str, "0");
            else strcpy(str, "1");
            argv[0] = str;

            if(cmd->stdout_file != NULL) {
                str = malloc(sizeof(cmd->stdout_file));
                strcpy(str, cmd->stdout_file);
                argv[num+1] = str;
            } else {
                str = malloc(sizeof(char));
                strcpy(str, "");
                argv[num+1] = str;
            }

            argv[num+2] = NULL;
        } else if(strcmp(job_name, "sleep") == 0) {
            func = shell_sleep;

            argv = malloc(sizeof(char*));

            // int time = atoi(cmd->commands[0][1]);
            char* str = malloc(sizeof(int));
            strcpy(str, cmd->commands[0][1]);
            
            argv[0] = str;
        } else if(strcmp(job_name, "echo") == 0) {
            func = shell_echo;

            // argv = malloc(sizeof(char*));
            // char* str = malloc(sizeof(command + 5)); //command e c h o " "
            // strcpy(str, command+5);
            // argv[0] = str;


            int num = 0;
            while(cmd->commands[0][num] != NULL) num++;
            num--; //remove the command name
            argv = malloc(sizeof(char*) * (num+1));
            for(int i = 1; i <= num; i++) {
                char* str = malloc(sizeof(cmd->commands[0][i]));
                strcpy(str, cmd->commands[0][i]);
                argv[i-1] = str;
            }
            argv[num] = NULL;
        } else if(strcmp(job_name, "busy") == 0) {
            func = shell_busy;
        } else if(strcmp(job_name, "touch") == 0) {
            func = shell_touch;

            int num = 0;
            while(cmd->commands[0][num] != NULL) num++;
            num--; //remove the command name
            argv = malloc(sizeof(char*) * (num+1));
            for(int i = 1; i <= num; i++) {
                char* str = malloc(sizeof(cmd->commands[0][i]));
                strcpy(str, cmd->commands[0][i]);
                argv[i-1] = str;
            }
            argv[num] = NULL;
        } else if(strcmp(job_name, "mv") == 0) {
            func = shell_move;

            argv = malloc(sizeof(char*) * 3);
            for(int i = 1; i <= 2; i++) {
                char* str = malloc(sizeof(cmd->commands[0][i]));
                strcpy(str, cmd->commands[0][i]);
                argv[i-1] = str;
            }
            argv[2] = NULL;
        } else if(strcmp(job_name, "cp") == 0) {
            func = shell_copy;

            argv = malloc(sizeof(char*) * 3);
            for(int i = 1; i <= 2; i++) {
                char* str = malloc(sizeof(cmd->commands[0][i]));
                strcpy(str, cmd->commands[0][i]);
                argv[i-1] = str;
            }
            argv[2] = NULL;
        } else if(strcmp(job_name, "rm") == 0) {
            func = shell_remove;

            int num = 0;
            while(cmd->commands[0][num] != NULL) num++;
            num--; //remove the command name
            argv = malloc(sizeof(char*) * (num+1));
            for(int i = 1; i <= num; i++) {
                char* str = malloc(sizeof(cmd->commands[0][i]));
                strcpy(str, cmd->commands[0][i]);
                argv[i-1] = str;
            }
            argv[num] = NULL;
        } else if(strcmp(job_name, "chmod") == 0) {
            func = shell_chmod;

            argv = malloc(sizeof(char*) * 3);
            for(int i = 1; i <= 2; i++) {
                char* str = malloc(sizeof(cmd->commands[0][i]));
                strcpy(str, cmd->commands[0][i]);
                argv[i-1] = str;
            }
            argv[2] = NULL;
        } else if(strcmp(job_name, "ps") == 0) {
            func = ps;
        } else if(strcmp(job_name, "kill") == 0) {
            func = shell_kill;

            int num = 0;
            while(cmd->commands[0][num] != NULL) num++;
            num--; //remove the command name
            argv = malloc(sizeof(char*) * (num+1));
            for(int i = 1; i <= num; i++) {
                char* str = malloc(sizeof(cmd->commands[0][i]));
                strcpy(str, cmd->commands[0][i]);
                argv[i-1] = str;
            }
            argv[num] = NULL;
        } else {
            //wait on children and continue
            wait_children();
            continue;
        }

        
        //handle redirections
        int fd1 = -1;
        int fd2 = -1;
        if(cmd->stdin_file != NULL) {
            fd1 = f_open(cmd->stdin_file, F_READ);
        }

        if(cmd->stdout_file != NULL) {
            if(cmd->is_file_append) fd2 = f_open(cmd->stdout_file, F_APPEND);
            else fd2 = f_open(cmd->stdout_file, F_WRITE);
        }

        pid_t pid;
        if(!nice) pid = p_spawn(func, argv, fd1, fd2);
        else pid = p_spawn_nice(func, argv, fd1, fd2, priority);

        if(fd1 != -1) f_close(fd1);
        if(fd2 != -1) f_close(fd2);

        if(!cmd->is_background) {
            //hand the process terminal control
            change_fg(pid);
            int status;
            p_waitpid(pid, &status, false);
            
            //take terminal control back
            change_fg(1); //shell will always be 1

            //check return status, if stopped, add job to job queue
            if(W_WIFSTOPPED(status)) {
                enqueue_job(pid, job_name, 1);
            }
        } else {
            job_node* job = enqueue_job(pid, job_name, 0);
            printf("Running [%d] %s\n", job->job_id, job->name);
        }

        //wait on all children
        wait_children();

        // printf("cmd[0][0]: %s", cmd->commands[0][0]);
        // printf("cmd[0][1]: %s", cmd->commands[0][1]);
        free(command);
        free(cmd);
    }
}

job_node* enqueue_job(pid_t pid, char* job_name, int stat) {
    //add the process to the job queue
    job_node* job = malloc(sizeof(job_node));
    job->name = malloc(strlen(job_name)+1);
    strcpy(job->name, job_name);
    job->pid = pid;
    job->status = stat;

    job_node* jptr = jobs;
    if(jobs == NULL) {
        job->job_id = 1;
        job->next = NULL;
        jobs = job;
    } else {
        while(jptr->next != NULL) jptr = jptr->next;
        job->job_id = jptr->job_id + 1;
        job->next = NULL;
        jptr->next = job;
    }

    return job;
}

void wait_children(void) {
    job_node* ptr = jobs;
    job_node* prev = NULL;
    while(ptr != NULL) {
        int status;
        if(p_waitpid(ptr->pid, &status, true) == -1) {
            prev = ptr;
            ptr = ptr->next;
        } else {
            //if it stopped update the job status, if it terminated
            //remove the job from the queue and report that it finished
            //to stdout
            if(W_WIFSTOPPED(status)) {
                ptr->status = 1;
                prev = ptr;
                ptr = ptr->next;
            } else {
                //terminated
                printf("Finished [%d] %s\n", ptr->job_id, ptr->name);
                free(ptr->name);
                if(prev == NULL) {
                    jobs = ptr->next;
                    prev = NULL;
                    free(ptr);
                    ptr = jobs;
                } else {
                    prev->next = ptr->next;
                    free(ptr);
                    ptr = prev->next;
                }
            }
        }
    }
}