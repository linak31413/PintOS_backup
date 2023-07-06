#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{ 
  /* [P2-1 NEW] Discarding existing two lines.
     Handle each system call handling cases. */
  switch (*(uint32_t *)f->esp)
  {
    case SYS_HALT :
      halt ();
      break;

    case SYS_EXIT :
      check_user_vaddr (f->esp+4);
      exit (*(int *)(f->esp+4));
      break;

    case SYS_CREATE :
      check_user_vaddr (f->esp+16);
      check_user_vaddr (f->esp+20);
      f->eax = create (*(const char **)(f->esp+16), *(unsigned *)(f->esp+20));
      break;

    case SYS_OPEN :
      check_user_vaddr (f->esp+4);
      f->eax = open (*(const char **)(f->esp+4));
      break;

    case SYS_WRITE :
      check_user_vaddr (f->esp+20);
      check_user_vaddr (f->esp+24);
      check_user_vaddr (f->esp+28);
      write (*(int *)(f->esp+20), *(const void **)(f->esp+24), *(unsigned *)(f->esp+28));
      break;

    case SYS_CLOSE :
      check_user_vaddr (f->esp+4);
      close (*(int *)(f->esp+4));
      break;

    default :
      exit (-1);
      break;
  }
}

/* [P2-1 NEW] Checking if the target address is in user space.
   If not(= kernel space), exit(-1). */
void
check_user_vaddr (const void *target)
{
  if(!is_user_vaddr (target))
    exit(-1);
}


/* [P2-1 NEW] halt, exit, create, open, write, close implemented. */

/* halt : Stop the whole PintOS system. */
void
halt (void)
{
  shutdown_power_off ();
}

/* exit : Unassign all file_desc[] information into NULL.
   Print out the target thread's name and its exit code.
   Then, thread_exit(). */
void
exit (int status)
{
  int i;
  for (i = 2; i < 64; i++)
  {
    if (thread_current()->file_desc[i] != NULL)
      close(i);
  }
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_exit();
}

/* create : Return filesys_create() if conditions are fulfilled.
   Return true when successfully created, false otherwise.
   Following cases are not normal:
     If file is NULL -> exit(-1).
     If file name is 0 or over READDIR_MAX_LEN -> return false. */
bool
create (const char *file, unsigned initial_size)
{
  if (&file == NULL)
  { 
    exit (-1);
  }
  int len = strlen (file);
  if (len == 0 || len > READDIR_MAX_LEN)
    return false;
  else
    return filesys_create (file, initial_size);
}

/* open : Perform filesys_open() if conditions are fulfilled.
   If successful, store filesys_open() result in file_desc[i].
   i is smallest index of file_desc[] that has NULL value.
   Return i upon successful open, -1 upon all other failures. */
int
open (const char *file)
{
  if (file == NULL)
  {
    exit (-1);
    return -1;
  }
  int len = strlen (file);
  if (len == 0 || len > READDIR_MAX_LEN)
  {
    return -1;
  }
  else
  {
    struct file *f = filesys_open (file);
    if (f == NULL)
    {
      return -1;
    }
    int i = 2;
    while (thread_current()->file_desc[i] != NULL)
      i++;
    thread_current()->file_desc[i] = f;
    return i;
  }
}

/* write : If fd is 1, write the result line.
   On other case, exit(-1). -> changed in Part 2-2! */
int
write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf (buffer, size);
    return size;
  }
  else
  {
    exit (-1);
  }
}

/* close : Close the file located in current thread's file_desc[fd].
   Clear file_desc[fd] as NULL, and file_close() that.
   exit(-1) if file_desc[fd] is already NULL. */
void
close (int fd)
{
  struct file *f = thread_current()->file_desc[fd];
  if (f == NULL)
    exit (-1);
  else
  {
    thread_current()->file_desc[fd] = NULL;
    file_close (f);
  }
}

