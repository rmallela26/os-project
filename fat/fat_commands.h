#ifndef FAT_COMMANDS
#define FAT_COMMANDS

typedef struct file_system {
    char* name;
    int fd;
    struct file_system* next;
    int fat_size;
    int block_size;
} file_system;

void mkfs(char* name, uint8_t num_blocks, uint8_t block_size_config);
void mount(char* name);
void umount(void);
int touch(char* name);
void ls(void);
int move(char* source, char* destination);
int remove_file(char* name);
int read_file(int n, char* buf, int mode, int fd, uint16_t block_num, off_t file_offset);
int write_file(int n, char* buf, int mode, int fd, uint16_t block_num, off_t file_offset, bool overwrite);
void concatenate(int num_inputs, char** names,  bool overwrite, char* output_name, int fd);
void copy(char* input, char* output, int mode);
int chmod(uint8_t new_mode, char* name); 

#endif