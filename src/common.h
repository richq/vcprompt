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

typedef struct vccontext_t vccontext_t;
struct vccontext_t {
    options_t* options;

    int (*probe)(vccontext_t*);
    result_t* (*get_info)(vccontext_t*);
};

void set_options(options_t*);
vccontext_t* init_context(options_t* options,
                          int (*probe)(vccontext_t*),
                          result_t* (*get_info)(vccontext_t*));
void free_context(vccontext_t* context);
    
result_t* init_result();
void free_result(result_t*);

void debug(char*, ...);

int isdir(char*);
int read_first_line(char*, char*, int);

#endif
