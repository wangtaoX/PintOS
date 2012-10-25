#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/vaddr.h"

#define DEFAULT_ARGS 30
#define STACK_PAGES 2000
#define STACK_ADDR_LIMIT (PHYS_BASE - (PGSIZE * STACK_PAGES))  

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
