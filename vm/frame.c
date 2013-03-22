#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"

static struct list_elem *evict_pointer;
/* Protect the frame table */
static struct lock frame_table_lock;
/* Frame hash table */
static struct list frame_list_table;
static int alloc_counts = 0;

static inline void update_frame_table(void *vir, void *p);

void frame_table_init(void)
{
  list_init(&frame_list_table);
  lock_init(&frame_table_lock);
  evict_pointer = list_head(&frame_list_table);
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
  {
    printf("alloc_counts %d no memory, should evict...\n", alloc_counts);
    p = evict_frame(vir);
  }
  if (!p)
    PANIC("user memory");
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

/* #### evict policy */
static struct list_elem *evict_pointer_next(struct list_elem *e)
{
  if (list_next(e) == list_tail(&frame_list_table))
    return list_next(list_head(&frame_list_table));

  return list_next(e);
}

#define frame_is_accessed(f) pagedir_is_accessed(f->owner->pagedir, f->uvir)
#define frame_is_writed(f) pagedir_is_dirty(f->owner->pagedir, f->uvir)
#define frame_set_not_accessed(f) pagedir_set_accessed(f->owner->pagedir, f->uvir, false)
//static bool frame_is_accessed(struct frame *f)
//{
//  return pagedir_is_accessed(f->owner->pagedir, f->uvir);
//}
//static bool frame_is_writed(struct frame *f)
//{
//  return pagedir_is_dirty(f->owner->pagedir, f->uvir);
//}
void *evict_frame(void *vir)
{
  struct frame *page;
  struct spt_general *sg;
  void *kvir;
  struct list_elem *e;
  struct spt_file *sf;

  if (evict_pointer == list_head(&frame_list_table))
  {
    printf("first evict...\n");
    evict_pointer = evict_pointer_next(evict_pointer);
  }
  page = list_entry(evict_pointer, struct frame, frame_list_elem);
  while(frame_is_accessed(page))
  {
    frame_set_not_accessed(page);

    evict_pointer = evict_pointer_next(evict_pointer);
    page = list_entry(evict_pointer, struct frame, frame_list_elem);
  }/* find the page*/

  if (!frame_is_accessed(page))
  {
    printf("find a page to evict...0x%x %s id(%d)\n", page->uvir, page->owner->name, page->owner->tid);
  }
  if (page == NULL)
    printf("Opoos, it`s a kernel bug...\n");
  sg = find_lazy_page_spt_entry(page->owner, page->uvir);
  if (sg == NULL)
  {
    printf("Opoos, it`s a bug...\n");
  }
  lock_acquire(&page->owner->spt_table_lock);
  
  sf = (struct spt_file *)sg;
  if (frame_is_writed(page))
    goto swap_to_disk;
  switch (sg->type)
  {
    case MMF:
    case FILE:
      sf->loaded = false;
      break;
    case SWAP:
      printf("it can`t be spt_swap_entry.\n");
      break;
    case ZERO:
      ((struct spt_zero *)sg)->loaded = false;
      break;
    default:
      printf("Opoos, it`s a bug...\n");
  }
//  if (frame_is_writed(page))
//  {
//    hash_delete(&page->owner->spt_table, &sg->spt_hash_elem);
//    printf("vir 0x%x\n", vir);
//    size_t index = swap_in(page);
//    struct spt_swap *ss = new_swap_spt_entry(vir, index);
//    add_spt_entry(page->owner, (struct spt_general *)ss);
//    printf("p : %s, tid :%d\n", thread_current()->name, thread_current()->tid);
//  } else { 
//    switch(sg->type) {
//      case MMF:
//      case FILE:
//        ((struct spt_file *)sg)->loaded = false;
//        break;
//      case ZERO:
//        break;
//     default:
//        printf("here it should not be a swap...\n");
//   }
//  }
  e = evict_pointer;
  evict_pointer = evict_pointer_next(evict_pointer);
  kvir = page->kvir;
  list_remove(e);
  pagedir_clear_page(page->owner->pagedir, page->uvir);
  lock_release(&page->owner->spt_table_lock);

  free(page);
  return kvir;

swap_to_disk:
  hash_delete(&page->owner->spt_table, &sf->spt_hash_elem);
  free(sf);

  size_t index = swap_in(page);
  struct spt_swap *ss = new_swap_spt_entry(page->uvir, index);
  add_spt_entry(page->owner, (struct spt_general *)ss);
  e = evict_pointer;
  evict_pointer = evict_pointer_next(evict_pointer);
  kvir = page->kvir;
  list_remove(e);
  pagedir_clear_page(page->owner->pagedir, page->uvir);
  lock_release(&page->owner->spt_table_lock);

  free(page);
  return kvir;
}
