#ifndef BUILTINS
#define BUILTINS

void print_jobs(void);
void fg(int id);
void bg(int id);
void nice_pid(int priority, pid_t pid);
void logout(void);

#endif