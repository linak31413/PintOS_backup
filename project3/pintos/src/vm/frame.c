#include "vm/frame.h"
#include <stdio.h>
#include "vm/page.h"
#include "devices/timer.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

static struct frame *frame_table;
static struct lock scan_lock;
static int frame_cnt;
static int hand;

/* Initialization for frame.c. */
void
frame_init (void)
{
  void *base_addr;

  lock_init (&scan_lock);
  
  frame_table = malloc (sizeof *frame_table * init_ram_pages);
  if (frame_table == NULL) /* Cannot make enough-sized frame table. */
    PANIC ("out of memory allocating page frames");

  while ((base_addr = palloc_get_page (PAL_USER)) != NULL) /* Until all memory is sliced. */
  {
    struct frame *fr = &frame_table[frame_cnt++];
    lock_init (&fr->fr_lock);
    fr->fr_base = base_addr;
    fr->fr_page = NULL;
  }
}

/* Free the frame that is held by page. */
void
frame_free (struct frame *fr)
{
  ASSERT (lock_held_by_current_thread (&fr->fr_lock));
          
  fr->fr_page = NULL;
  lock_release (&fr->fr_lock);
}

/* One attempt to make a mapping between frame<->page.
   Return trun upon success, false on failure. */
static struct frame *
frame_alloc_lock_try (struct page *pg) 
{
  lock_acquire (&scan_lock);

  /* Try to find a free frame at once. */
  int i;
  for (i = 0; i < frame_cnt; i++)
  {
    struct frame *fr = &frame_table[i];
    
    if (!lock_try_acquire (&fr->fr_lock))
      continue;
    if (fr->fr_page == NULL) /* Found empty frame. */
    {
      fr->fr_page = pg;
      lock_release (&scan_lock);
      return fr;
    } 
    lock_release (&fr->fr_lock);
  }

  /* Reaching here means there was no free frame.
     Need to choose a victim frame. */
  for (i = 0; i < frame_cnt * 2; i++) 
  {
    struct frame *fr = &frame_table[hand];
    hand = (hand + 1) % frame_cnt;
    
    if (!lock_try_acquire (&fr->fr_lock))
      continue;
    if (fr->fr_page == NULL) /* Found empty frame now. */
    {
      fr->fr_page = pg;
      lock_release (&scan_lock);
      return fr;
    } 
    if (page_has_accessed (fr->fr_page)) /* Recent access happened. Pass on this frame. */
    {
      lock_release (&fr->fr_lock);
      continue;
    }
    lock_release (&scan_lock);
      
    /* Victim chosen: evict this frame and replace it. */
    if (!page_out (fr->fr_page))
    {
      lock_release (&fr->fr_lock);
      return NULL;
    }
    fr->fr_page = pg;
    return fr;
  }

  lock_release (&scan_lock);
  return NULL;
}


/* Do 3 attempts to match page<->frame.
   Returns the frame upon success, false on failure. */
struct frame *
frame_alloc_lock (struct page *pg) 
{
  int try;
  for (try = 0; try < 3; try++) 
  {
    struct frame *fr = frame_alloc_lock_try (pg);
    if (fr != NULL) 
    {
      ASSERT (lock_held_by_current_thread (&fr->fr_lock));
      return fr; 
    }
    timer_msleep (1000); /* Have some cooltime between attempts. */
  }
  return NULL;
}

/* Locks give page's mapped frame into memory.
   After this function, pg->pg_frame cannot be changed until unlock. */
void
frame_lock (struct page *pg) 
{
  /* A frame can be asynchronously removed, but never inserted. */
  struct frame *fr = pg->pg_frame;
  if (fr != NULL) 
    {
      lock_acquire (&fr->fr_lock);
      if (fr != pg->pg_frame)
        {
          lock_release (&fr->fr_lock);
          ASSERT (pg->pg_frame == NULL); 
        } 
    }
}

/* Unlocks the given frame that has been locked on current_thread. */
void
frame_unlock (struct frame *fr) 
{
  ASSERT (lock_held_by_current_thread (&fr->fr_lock));
  lock_release (&fr->fr_lock);
}