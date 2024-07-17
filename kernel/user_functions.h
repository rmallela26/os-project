#ifndef USER_FUNCTIONS
#define USER_FUNCTIONS

bool check_exited(int status);
bool check_stopped(int status);
bool check_signaled(int status);

#define W_WIFEXITED(status) check_exited(status)
#define W_WIFSTOPPED(status) check_stopped(status)
#define W_WIFSIGNALED(status) check_signaled(status)

pid_t p_spawn(void (*func)(), char *argv[], int fd0, int fd1);
pid_t p_spawn_nice(void (*func)(), char *argv[], int fd0, int fd1, int priority);
pid_t p_waitpid(pid_t pid, int *wstatus, bool nohang);
int p_kill(pid_t pid, int sig);
void p_exit(void);
int p_nice(pid_t pid, int priority);
void p_sleep(unsigned int ticks);
void change_fg(pid_t pid);
void list_processes(void);

#endif