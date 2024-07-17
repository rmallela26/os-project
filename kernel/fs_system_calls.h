#ifndef FS_SYSTEM_CALLS
#define FS_SYSTEM_CALLS

#define F_WRITE 0
#define F_READ 1
#define F_APPEND 2

#define F_SEEK_SET 0
#define F_SEEK_CUR 1
#define F_SEEK_END 2

#include "data_structs.h"

int f_open(char *fname, int mode);
int f_read(int fd, char *buf, int n);
int f_write(int fd, char *str, int n);
void f_close(int fd);
int f_unlink(char* fname);
void f_lseek(int fd, int offset, int whence);
int duplicate(int oldfd, int newfd, pcb* process);

#endif