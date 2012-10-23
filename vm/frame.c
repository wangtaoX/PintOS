#include "frame.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Protect the frame table */
static struct lock frame_table_lock;
/* Frame hash table */
static struct list frame_list_table;
static int alloc_counts = 0;

static inline void update_frame_table(void *vir, void *p);

void frame_table_init()
{
  list_init(&frame_list_table);
  lock_init(&frame_table_lock);
  printf("frame table initialize...\n");
}

static inline void update_frame_table(void *vir, void *p)
{
  struct thread *cur = thread_current();
  struct frame *f = (struct frame *)malloc(sizeof(struct frame));

  f->pining = false;
  f->uvir = vir;
  f->kvir = p;
  f->owner = cur;
  list_push_back(&frame_list_table, &f->frame_list_elem);
}

void *frame_get_page(enum palloc_flags flag, void *vir)
{
  void *p = NULL;

  if (!flag)
    return p;

  lock_acquire(&frame_table_lock);
  p = palloc_get_page(flag);
  if (p == NULL)
    PANIC("malloc");
  update_frame_table(vir, p);
  alloc_counts += 1;
  lock_release(&frame_table_lock);

  return p;
}
void frame_free_page(void *kpage)
{
  struct frame *f;
  struct list_elem *e;

  if (kpage == NULL)
    return;

  lock_acquire(&frame_table_lock);
  for (e = list_begin(&frame_list_table); e != list_end(&frame_list_table);
      e = list_next(e))
  {
    f = list_entry(e, struct frame, frame_list_elem);
    if (f->kvir == kpage)
    {
      list_remove(e);
      free(f);
      palloc_free_page(kpage);
      break;
    }
  }
  alloc_counts -= 1;
  lock_release(&frame_table_lock);

  return;
}


void debug_frame_table(void)
{
  struct list_elem *e;

  lock_acquire(&frame_table_lock);
  if (!list_empty(&frame_list_table))
    printf("Debug frame table alloc_counts %d\n", alloc_counts);
  else
    printf("Empty frame table\n");
  for (e = list_begin(&frame_list_table); e != list_end(&frame_list_table);
      e = list_next(e))
  {
    struct frame *f = list_entry(e, struct frame, frame_list_elem);
    printf("Vir 0x%-10x to p 0x%-10x, Owner[%s]\n", f->uvir, f->kvir, f->owner->name);
  }
  lock_release(&frame_table_lock);
}
