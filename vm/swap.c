#include <bitmap.h>
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>
#include "swap.h"

struct block *swap_device;

static struct lock swap_lock;
static struct bitmap *swap_table;

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)
#define swap_size_in_page() (block_size(swap_device) / SECTORS_PER_PAGE)

void
swap_init ()
{
  /* init the swap device */
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("no swap device found, can't initialize swap");

  /* init the swap bitmap */
  swap_table = bitmap_create (swap_size_in_page ());
  if (swap_table == NULL)
    PANIC ("swap bitmap creation failed");

  /* initialize all bits to be true */ 
  bitmap_set_all (swap_table, false);
  lock_init(&swap_lock);
  printf("swap size in page %d...\n", swap_size_in_page());
}

//static size_t swap_size_in_page(void)
//{
//  return block_size(swap_device) / SECTORS_PER_PAGE;
//}
size_t swap_in(struct frame *f)
{
  ASSERT(f != NULL);


  lock_acquire(&swap_lock);
  printf("write to swap...\n");
  size_t index = bitmap_scan_and_flip(swap_table, 0, 1, false);
  if (index == BITMAP_ERROR)
    return SWAP_ERROR;

  size_t counter = 0;
  while (counter < SECTORS_PER_PAGE)
  {
    block_write(swap_device, index * SECTORS_PER_PAGE + counter,
        f->kvir + counter * BLOCK_SECTOR_SIZE);
    counter++;
  }
  lock_release(&swap_lock);
  printf("write complete...\n");

  return index;
}

bool swap_out(size_t idx, void *f)
{
  ASSERT(f != NULL);

  lock_acquire(&swap_lock);
  printf("swap out...\n");
  bitmap_set(swap_table, idx, false);

  size_t counter = 0;
  while (counter < SECTORS_PER_PAGE)
  {
    block_read(swap_device, idx * SECTORS_PER_PAGE + counter, 
        f + counter * BLOCK_SECTOR_SIZE);
    counter++;
  }
  printf("swap out complete...\n");
  lock_release(&swap_lock);

  return true;
}

void swap_release(size_t idx)
{
  lock_acquire(&swap_lock);
  bitmap_flip(swap_table, idx);
  lock_release(&swap_lock);
}
