#include "vm/page.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

#define PHYS_LIMIT 2^20

/* Destroy each page that is in the current thread's page table. */
static void
page_exit_each (struct hash_elem *p, void *aux UNUSED)
{
  struct page *pg = hash_entry (p, struct page, pgt_elem);
  frame_lock (pg);
  if (pg->pg_frame)
    frame_free (pg->pg_frame);
  free (pg);
}

/* Destroys the current thread's page table. */
void
page_exit (void)
{
  struct hash *pg_table = thread_current ()->page_table;
  if (pg_table != NULL)
    hash_destroy (pg_table, page_exit_each);
}

/* Using given address, try to find a page.
   If failed to find, NULL pointer will given. */
static struct page *
page_from_addr (const void *target_addr)
{
  if (target_addr >= PHYS_BASE)
    return NULL;
  
  struct page pg;
  struct hash_elem *e;

  pg.pg_addr = (void *) pg_round_down (target_addr);
  e = hash_find (thread_current ()->page_table, &pg.pgt_elem);
  
  if (e != NULL)
    return hash_entry (e, struct page, pgt_elem);
  if (pg.pg_addr > PHYS_BASE - PHYS_LIMIT  && (void *) thread_current ()->user_esp - 32 < target_addr)
    return page_allocate (pg.pg_addr, false);
}

/* Locks a frame for page P and pages it in.
   Return true on success, false on failure. */
static bool
page_in_frame (struct page *pg)
{
  /* Get a frame for the page. */
  pg->pg_frame = frame_alloc_lock (pg);
  if (pg->pg_frame == NULL)
    return false;

  /* Copy data into the frame, from swap or file. */
  if (pg->pg_sector != (block_sector_t) -1) /* Data driven from swapping. */
    swap_in (pg);
  else if (pg->pg_file != NULL) /* Data driven from file. */
  {
    off_t bytes_read = file_read_at (pg->pg_file, pg->pg_frame->fr_base, pg->pg_file_bytes, pg->pg_file_offset);
    off_t bytes_origin = PGSIZE - bytes_read;
    memset (pg->pg_frame->fr_base + bytes_read, 0, bytes_origin);
    if (bytes_read != pg->pg_file_bytes)
      printf ("bytes read (%"PROTd") != bytes requested (%"PROTd")\n", bytes_read, pg->pg_file_bytes);
  }
  else /* No swap & file: all-zero page. */
    memset (pg->pg_frame->fr_base, 0, PGSIZE);

  return true;
}

/* Try to put target page and its frame into the page table.
   This will be called upon page fault.
   Return true on success, false on failure. */
bool
page_in (void *target_addr)
{
  struct page *pg;
  bool success = false;

  if (thread_current ()->page_table == NULL) /* No hash table. */
    goto done;
  pg = page_from_addr (target_addr);
  if (pg == NULL) /* Failed to get a page from address. */
    goto done;
  frame_lock (pg);
  if (pg->pg_frame == NULL) /* Page not having frame for now. */
    if (!page_in_frame (pg)) /* Failed to allocate a new frame. */
      goto done;
      
  ASSERT (lock_held_by_current_thread (&pg->pg_frame->fr_lock));

  /* Put page<->frame link into the page table. */
  success = pagedir_set_page (thread_current ()->pagedir, pg->pg_addr, pg->pg_frame->fr_base, !pg->pg_read_only);

  frame_unlock (pg->pg_frame);
 done:
  return success;
}

/* Take out the given page from page table.
   Then delink the frame connected with the page. */
bool
page_out (struct page *pg)
{
  bool frame_dirty;
  bool success = false;

  ASSERT (pg->pg_frame != NULL);
  ASSERT (lock_held_by_current_thread (&pg->pg_frame->fr_lock));

  /* Take out this page from page table priorly. */
  pagedir_clear_page(pg->pg_thread->pagedir, (void *) pg->pg_addr);
  /* Check if frame has history of modification. */
  frame_dirty = pagedir_is_dirty (pg->pg_thread->pagedir, (const void *) pg->pg_addr);

  if(frame_dirty) /* Changes were on frame. Consider writing on disk. */
  {
    if(pg->pg_private) /* Private page; just perform swap out. */
      success = swap_out (pg);
    else /* Not private page; then we have to write file on disk. */
      success = file_write_at (pg->pg_file, (const void *) pg->pg_frame->fr_base, pg->pg_file_bytes, pg->pg_file_offset);
  }
  else /* No change on frame. */
  {
    success = true;
    if (pg->pg_file == NULL) /* No file; just swap out right away. */
      success = swap_out (pg);
  }

  /* De-link the frame with the page. */
  if (success)
    pg->pg_frame = NULL;
  return success;
}

/* Make a mapping between page and page table.
   If page already exists, it fails to allocate. */
struct page *
page_allocate (void *target_addr, bool read_only)
{
  struct page *pg = malloc (sizeof *pg);
  if (pg != NULL)
    {
      pg->pg_thread = thread_current ();
      pg->pg_addr = pg_round_down (target_addr);
      pg->pg_frame = NULL;

      pg->pg_read_only = read_only;
      pg->pg_private = !read_only;

      pg->pg_sector = (block_sector_t) -1;
      pg->pg_file = NULL;
      pg->pg_file_offset = 0;
      pg->pg_file_bytes = 0;

      if (hash_insert (thread_current ()->page_table, &pg->pgt_elem) != NULL) /* Already mapped. */
        free (pg);
        pg = NULL;
    }
  return pg;
}

/* Delete a mapping between page and page table. */
void
page_deallocate (void *target_addr)
{
  struct page *pg = page_from_addr (target_addr);
  ASSERT (pg != NULL);
  
  frame_lock (pg);
  if (pg->pg_frame)
  {
    if (pg->pg_file && !pg->pg_private)
    page_out (pg);
    frame_free (pg->pg_frame);
  }
  hash_delete (thread_current ()->page_table, &pg->pgt_elem);
  free (pg);
}

/* Get the page from given address, and lock that page with the frame. */
bool
page_lock (const void *target_addr, bool for_write)
{
  struct page *pg = page_from_addr (target_addr);
  
  if (pg == NULL) /* No page found from address. */
    return false;
  if (pg->pg_read_only && for_write) /* Conflicting condition. */
    return false;

  frame_lock (pg);
  if (pg->pg_frame == NULL) /* No frame for this page; try to allocate. */
    return (page_in_frame (pg) && pagedir_set_page (thread_current ()->pagedir, pg->pg_addr, pg->pg_frame->fr_base, !pg->pg_read_only));
  else
    return true;
}

/* Unlocks the page<->frame relationship. */
void
page_unlock (const void *target_addr)
{
  struct page *pg = page_from_addr (target_addr);
  ASSERT (pg != NULL);
  frame_unlock (pg->pg_frame);
}

/* Check if the given page(with frame) has history of access. */
bool
page_has_accessed (struct page *pg)
{
  bool result;

  ASSERT (pg->pg_frame != NULL);
  ASSERT (lock_held_by_current_thread (&pg->pg_frame->fr_lock));

  result = pagedir_is_accessed (pg->pg_thread->pagedir, pg->pg_addr);
  if (result)
    pagedir_set_accessed (pg->pg_thread->pagedir, pg->pg_addr, false);
  return result;
}

/* For hash initialization : hash_hash_func */
unsigned
page_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *pg = hash_entry (e, struct page, pgt_elem);
  return ((uintptr_t) pg->pg_addr) >> PGBITS;
}

/* For hash initialization : hash_less_func */
bool
page_less_func (const struct hash_elem *pgt_elem1, const struct hash_elem *pgt_elem2, void *aux UNUSED)
{
  const struct page *pg1 = hash_entry (pgt_elem1, struct page, pgt_elem);
  const struct page *pg2 = hash_entry (pgt_elem2, struct page, pgt_elem);

  return pg1->pg_addr < pg2->pg_addr;
}