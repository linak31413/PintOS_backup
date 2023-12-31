#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <debug.h>
#include <stdbool.h>

struct page;
void swap_init (void);
void swap_in (struct page *);
bool swap_out (struct page *);

#endif /* vm/swap.h */