# OS

The kernel folder includes all of the code for the operating system. The fat folder includes the code for implementing a FAT filesystem. 

## Kernel

The kernel has a round-robin scheduler with three priority levels to schedule processes (implemented as ucontext threads). It has kernel level functions that are not visible to user processes (such as the shell), and are called by user level functions which are similar to system calls. 

## Shell

The shell implements foreground and background functionality, job control, synchronous child waiting, redirections with truncate and append modes, and built-in functions such as echo, cat, sleep (non busy waiting) etc. 

## FAT

The FAT is implemented as a single file on the host OS, with 16 bit addressability. The user manipulates it through filesystem system calls, and can customize the FAT size and block size. 