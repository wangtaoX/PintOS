#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"

struct frame
{
  /* Referrence counts */
  bool pining;
  /* Virtual address of this frame */
  void *uvir;
  /* Kernel virtual address */
  void *kvir;
  /* Which thread obtained this frame */
  struct thread *owner;
  /* Hash element */
  struct list_elem frame_list_elem;
};

/* Frame table function */
void frame_table_init(void);
void *frame_get_page(enum palloc_flags flag, void *vir);
void frame_free_page(void *p);
void *evict_frame(void *vir);
void debug_frame_table(void);
#endif
