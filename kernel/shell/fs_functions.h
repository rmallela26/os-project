#ifndef FS_FUNCTIONS
#define FS_FUNCTIONS

void shell_ls(void);
void cat(char* argv[]);
// void shell_sleep(int n);
void shell_echo(char* argv[]);
void shell_busy(void);
void shell_touch(char* argv[]);
void shell_move(char* argv[]);
void shell_copy(char* argv[]);
void shell_remove(char* argv[]);
void shell_chmod(char* argv[]);

#endif