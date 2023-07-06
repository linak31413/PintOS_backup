#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include "threads/synch.h"

/* A physical frame. */
struct frame 
  {
    struct lock fr_lock;           /* Locking frame for race condition. */
    void *fr_base;                 /* Frame's kernel base address. */
    struct page *fr_page;          /* Store mapped page struct. */
  };

void frame_init (void);

void frame_free (struct frame *);

void frame_lock (struct page *);
void frame_unlock (struct frame *);

struct frame *frame_alloc_lock (struct page *);

#endif /* vm/frame.h */