#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);

struct file 
{
  struct inode *inode;
  off_t pos;
  bool deny_write;
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  /* [P2-1 NEW] Discarding existing two lines.
     Handle each system call handling cases.
     [P2-2 NEW] All syscall implemented. */
  switch (*(uint32_t *)f->esp)
  {
    case SYS_HALT :
      halt ();
      break;

    case SYS_EXIT :
      check_user_vaddr (f->esp+4);
      exit (*(int *)(f->esp+4));
      break;
    
    case SYS_EXEC :
      check_user_vaddr (f->esp+4);
      f->eax = exec (*(const char **)(f->esp+4));
      break;
   
    case SYS_WAIT :
      check_user_vaddr (f->esp+4);
      f->eax = wait (*(pid_t *)(f->esp+4));
      break;
 
    case SYS_CREATE :
      check_user_vaddr (f->esp+16);
      check_user_vaddr (f->esp+20);
      f->eax = create (*(const char **)(f->esp+16), *(unsigned *)(f->esp+20));
      break;

    case SYS_REMOVE :
      check_user_vaddr (f->esp+4);
      f->eax = remove (*(const char **)(f->esp+4));
      break;

    case SYS_OPEN :
      check_user_vaddr (f->esp+4);
      f->eax = open (*(const char **)(f->esp+4));
      break;

    case SYS_FILESIZE :
      check_user_vaddr (f->esp+4);
      f->eax = filesize (*(int *)(f->esp+4));
      break;

    case SYS_READ :
      check_user_vaddr (f->esp+20);
      check_user_vaddr (f->esp+24);
      check_user_vaddr (f->esp+28);
      f->eax = read (*(int *)(f->esp+20), *(void **)(f->esp+24), *(unsigned *)(f->esp+28));
      break;

    case SYS_WRITE :
      check_user_vaddr (f->esp+20);
      check_user_vaddr (f->esp+24);
      check_user_vaddr (f->esp+28);
      f->eax = write (*(int *)(f->esp+20), *(const void **)(f->esp+24), *(unsigned *)(f->esp+28));
      break;
    
    case SYS_SEEK :
       check_user_vaddr (f->esp+16);
       check_user_vaddr (f->esp+20);
       seek (*(int *)(f->esp+16), *(unsigned *)(f->esp+20));
       break;

    case SYS_TELL :
       check_user_vaddr (f->esp+4);
       f->eax = tell (*(int *)(f->esp+4));
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
    exit (-1);
}


/* [P2-1 NEW] halt, exit, create, open, write, close implemented. */
/* [P2-2 NEW] exec, wait, remove, filesize, read, write, seek, tell implemented. */

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
  struct thread *thread_crnt = thread_current ();
  thread_crnt->exit_status = status;
  
  for (i = 2; i < 128; i++)
    if (thread_crnt->file_desc[i] != NULL)
      close (i);
  
  struct thread *thread_temp;
  struct list_elem *sibl_list_elem;
  for (sibl_list_elem = list_begin (&thread_crnt->sibling_list); sibl_list_elem != list_end (&thread_crnt->sibling_list); sibl_list_elem = list_next (sibl_list_elem))
  {
    thread_temp = list_entry (sibl_list_elem, struct thread, child_list_elem);
    wait (thread_temp->tid);
  }

  printf ("%s: exit(%d)\n", thread_name (), status);
  thread_exit ();
}

/* exec : execute the process with given function process_execute(). */
pid_t
exec (const char *cmd_line)
{
  char *cmd_copy = palloc_get_page(0);
  if (cmd_copy == NULL) return -1;
  strlcpy(cmd_copy, cmd_line, strlen(cmd_line) + 1);
  pid_t pid = process_execute (cmd_copy);
  palloc_free_page (cmd_copy);
   
  int a = 1;
  while (a < 100000) a++;
  
  return pid;
  //return process_execute (cmd_line);
}

/* wait : wait for child process to end, with given function process_wait(). */
int
wait (pid_t pid)
{
  pid_t ret = process_wait (pid);
  return ret;
  //return process_wait (pid);
}

/* remove : Return filesys_remove() if conditions are fulfilled.
   Return true when successfully removed, false otherwise.
   Following cases are not normal:
     If file is NULL -> exit(-1).
     If file name length is 0 or over READDIR_MAX_LEN -> return false. */
bool
remove (const char *file)
{
  int len = strlen (file); 
  if (len == 0 || len > READDIR_MAX_LEN)
    return false;
  else
    return filesys_remove (file);
}

/* create : Return filesys_create() if conditions are fulfilled.
   Return true when successfully created, false otherwise.
   Following cases are not normal:
     If file is NULL -> exit(-1).
     If file name length is 0 or over READDIR_MAX_LEN -> return false. */
bool
create (const char *file, unsigned initial_size)
{
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
  }
  int len = strlen (file);
  if (len == 0 || len > READDIR_MAX_LEN)
  {
    return -1;
  }
  else
  {
    int i = 2;
    for (;i < 128; i++)
    {
      if (thread_current()->file_desc[i] == NULL)
        break;
    }
    if (i > 127)
    {
      return -1;
    }

    struct file *f = filesys_open (file);
    if (f == NULL)
      return -1;

    if (strcmp(thread_current()->name, file) == 0)
      file_deny_write (f);
    
    thread_current()->file_desc[i] = f;
    return i;
  }
}

/* filesize : Getting the file size of the given folder descriptor.
   If current thread is not having such fd, exit with -1. */
int
filesize (int fd)
{
  struct file *f = thread_current()->file_desc[fd];
  if (f == NULL)
    exit (-1);
  else
    return file_length (f);
}

/* read : If fd is 0, read from user input.
   On other case, read from file if fd exists.*/
int
read (int fd, void *buffer, unsigned size)
{
  check_user_vaddr (buffer);
  if (fd == 0)
  {
    input_getc ();
    return size;
  }
  struct file *f = thread_current()->file_desc[fd];
  if (f == NULL)
    exit (-1);
  return file_read (f, buffer, size);
}

/* write : If fd is 1, write the result line.
   On other case, write in file if fd exists. */
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
    struct file *f = thread_current()->file_desc[fd];
    
    if (f == NULL)
      exit (-1);
    if (f->deny_write == true)
      return 0;

    file_write (f, buffer, size);
    return size;
  }
}

/* seek : Checks if there is file on the certain file descriptor index.
   If exists, change file's position to ginven unsigned position. */
void
seek (int fd, unsigned position)
{
  struct file *f = thread_current()->file_desc[fd];
  if (f == NULL)
    exit (-1);
  file_seek (f, position);
}

/* tell : Checks if there is file on the certain file descriptor index.
   If exists, return file's unsigned position. */
unsigned
tell (int fd)
{
  struct file *f = thread_current()->file_desc[fd];
  if (f == NULL)
    exit (-1);
  return file_tell (f);
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
    file_allow_write (f);
    thread_current()->file_desc[fd] = NULL;
    file_close (f);
  }
}

