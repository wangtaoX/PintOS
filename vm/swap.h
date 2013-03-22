#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "vm/frame.h"

#define SWAP_ERROR SIZE_MAX
void swap_init(void);
size_t swap_in(struct frame *f);
bool swap_out(size_t idx, void *f);
void swap_release(size_t idx);
#endif
