#ifndef VCPROMPT_H
#define VCPROMPT_H

typedef struct {
    int debug;
    int branch;                         /* show current branch? */
    int unknown;                        /* show ? if unknown files? */
    int modified;                       /* show ! if local changes? */
} options_t;

typedef struct {
    char* branch;                       /* name of branch */
    int unknown;                        /* any unknown files? */
    int modified;                       /* any local changes? */
} result_t;

void set_options(options_t*);
    
result_t* init_result();
void free_result(result_t*);

void debug(char*, ...);

int isdir(char*);
int read_first_line(char*, char*, int);

#endif
