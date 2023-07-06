#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <hash.h>
#include "devices/block.h"
#include "filesys/off_t.h"
#include "threads/synch.h"

struct page
  {
    struct thread *pg_thread;    /* Thread that owns this page. */
    void *pg_addr;               /* Address of this page. */
    struct frame *pg_frame;      /* Allocated frame to this page. */
    struct hash_elem pgt_elem;   /* Page table element. */
    
    bool pg_read_only;           /* Is this page read only? */
    bool pg_private;             /* Is this page private? */
    
    block_sector_t pg_sector;    /* Starting sector of swap area. */
    struct file *pg_file;        /* Page's file struct. */
    off_t pg_file_offset;        /* Offset in the file. */
    off_t pg_file_bytes;         /* Offset in file to read&write. */
  };

/* Destory current thread's page table. */
void page_exit (void);

/* Connect/Disconnect page and frame and
   put in/take out the page table. */
bool page_in (void *);
bool page_out (struct page *);

/* Make a mapping between page and page table. */
struct page *page_allocate (void *, bool);    
void page_deallocate (void *);

/* Lock/Unlock the given page with mapped frame. */
bool page_lock (const void *, bool);
void page_unlock (const void *);

/* Check if the given page has history of access. */
bool page_has_accessed (struct page *);

hash_hash_func page_hash_func;
hash_less_func page_less_func;

#endif /* vm/page.h */
