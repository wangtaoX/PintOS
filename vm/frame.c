#include "frame.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Protect the frame table */
static struct lock frame_table_lock;
/* Frame hash table */
static struct list frame_list_table;

static inline void update_frame_table(void *vir, void *p);

void frame_table_init()
{
  list_init(&frame_list_table);
  lock_init(&frame_table_lock);
}

static inline void update_frame_table(void *vir, void *p)
{
  struct thread *cur = thread_current();
  struct frame *f = (struct frame *)malloc(sizeof(struct frame));

  f->virtual = vir;
  f->owner = cur;
  list_push_back(&frame_list_table, &f->frame_list_elem);
}

void *frame_get_page(enum palloc_flags flag, void *vir)
{
  void *p = NULL;

  if (flag)
    return p;

  p = palloc_get_page(flag);
  lock_acquire(&frame_table_lock);
  if (p == NULL)
    //p = evict_page();
  update_frame_table(vir, p);
  lock_release(&frame_table_lock);

  return p;
}
