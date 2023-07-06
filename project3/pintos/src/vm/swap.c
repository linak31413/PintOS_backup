#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* The swap device. */
static struct block *swap_block;

/* Used swap pages. */
static struct bitmap *swap_bitmap;

/* Protects swap_bitmap. */
static struct lock swap_lock;

/* Number of sectors per page. */
#define PAGE_SECTORS (PGSIZE / BLOCK_SECTOR_SIZE)

/* Initialization on swap.c. */
void
swap_init (void)
{
  swap_block = block_get_role (BLOCK_SWAP);
  if (swap_block == NULL) /* Swapping unavailable. */
  {
    printf ("no swap device--swap disabled\n");
    swap_bitmap = bitmap_create (0);
  }
  else /* Swapping available. */
    swap_bitmap = bitmap_create (block_size (swap_block) / PAGE_SECTORS);
    
  if (swap_bitmap == NULL)
    PANIC ("couldn't create swap bitmap");
  lock_init (&swap_lock);
}

/* Swaps in page P(that has frame mapped), into swap_block. */
void
swap_in (struct page *pg)
{
  ASSERT (pg->pg_frame != NULL);
  ASSERT (lock_held_by_current_thread (&pg->pg_frame->fr_lock));
  ASSERT (pg->pg_sector != (block_sector_t) -1);
  
  int i;
  for (i = 0; i < PAGE_SECTORS; i++)
    block_read (swap_block, pg->pg_sector + i, pg->pg_frame->fr_base + i * BLOCK_SECTOR_SIZE);
  bitmap_reset (swap_bitmap, pg->pg_sector / PAGE_SECTORS);
  pg->pg_sector = (block_sector_t) -1;
}

/* From swap_block, swaps out page P. */
bool
swap_out (struct page *pg)
{
  ASSERT (pg->pg_frame != NULL);
  ASSERT (lock_held_by_current_thread (&pg->pg_frame->fr_lock));

  lock_acquire (&swap_lock);
  size_t slot = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  lock_release (&swap_lock);
  
  if (slot == BITMAP_ERROR)
    return false;
  pg->pg_sector = slot * PAGE_SECTORS;

  /*  Write out page sectors for each modified block. */
  int i;
  for (i = 0; i < PAGE_SECTORS; i++)
    block_write (swap_block, pg->pg_sector + i, (uint8_t *) pg->pg_frame->fr_base + i * BLOCK_SECTOR_SIZE);

  pg->pg_private = false;
  pg->pg_file = NULL;
  pg->pg_file_offset = 0;
  pg->pg_file_bytes = 0;

  return true;
}