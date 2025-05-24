#include "threads/synch.h"
#include "lib/user/syscall.h"


#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

struct lock filesys_lock;
struct lock fork_lock;

#endif /* userprog/syscall.h */
