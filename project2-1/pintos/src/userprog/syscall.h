#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "lib/user/syscall.h"

void syscall_init (void);

void check_user_vaddr (const void *);
void halt (void);
void exit (int);
bool create (const char *, unsigned);
int open (const char *);
int write (int, const void *, unsigned);
void close (int);

#endif /* userprog/syscall.h */
