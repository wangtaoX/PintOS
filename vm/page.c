#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include <string.h>

/* #### hash function & some others */
static struct spt_zero *new_zero_spt_entry(void *uva, bool writeable);
static unsigned spt_hash_func(const struct hash_elem *e, void *aux UNUSED);
/* spt hash less function */
static bool spt_hash_less_func(const struct hash_elem *a,
                               const struct hash_elem *b,
                               void *aux UNUSED);
static void spt_hash_destroy_func(struct hash_elem *e, void *aux UNUSED);
/* return a new file entry */
static struct spt_file *new_file_spt_entry(struct file *f, void *uva, off_t offset,
    size_t prb, size_t pzb, bool writeable, enum spt_type type);
/* ####*/


/* #### load function */
static bool load_swap_page(struct spt_general *sg);
static bool load_file_page(struct spt_general *sg);
static bool load_zero_page(struct spt_general *sg);
/* ####*/




/* spt hash function */
static unsigned spt_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
  struct spt_general *sg = hash_entry(e, struct spt_general, spt_hash_elem);
  return hash_bytes(&sg->vaddr, sizeof(sg->vaddr));
}

/* spt hash less function */
static bool spt_hash_less_func(const struct hash_elem *a,
                               const struct hash_elem *b,
                               void *aux UNUSED)
{
  struct spt_general *a_ = hash_entry(a, struct spt_general, spt_hash_elem);
  struct spt_general *b_ = hash_entry(b, struct spt_general, spt_hash_elem);

  /* Return true if a->addr < b->addr */
  return a_->vaddr < b_->vaddr;
}

/* initialize the per-process spt */
void init_spt_table(struct thread *t)
{ 
  hash_init(&t->spt_table, spt_hash_func, spt_hash_less_func, NULL);
  lock_init(&t->spt_table_lock);
//  printf("Per-process spt initialize complete...\n");
}

/* add a new spt entry in spt */
void add_spt_entry(struct thread *t, struct spt_general *sg)
{
  ASSERT(t != NULL && sg != NULL);

  hash_insert(&t->spt_table, &sg->spt_hash_elem); 
}

struct spt_general *new_spt_entry(struct file *f, void *uva, off_t offset,
    size_t prb, size_t pzb, bool writeable, enum spt_type type)
{
  switch (type)
  {
    case ZERO:
      return new_zero_spt_entry(uva, writeable);
    case MMF:
    case FILE:
      return new_file_spt_entry(f, uva, offset, prb, pzb, writeable, type);
    default :
      return NULL;
  }
}

struct spt_swap * new_swap_spt_entry(void *uva, size_t index)
{
  struct spt_swap *ss = malloc(sizeof(struct spt_swap));

  if (ss == NULL)
    PANIC("malloc");

  ss->type = SWAP;
  ss->vaddr = uva;
  ss->idx = index;
  ss->writeable = true;
  ss->loaded = false;

  return ss;
}

static struct spt_zero *new_zero_spt_entry(void *uva, bool writeable)
{
  struct spt_zero *sz = malloc(sizeof(struct spt_zero));

  if (sz == NULL)
    return sz;
  sz->type = ZERO;
  sz->vaddr = uva;
  sz->writeable = writeable;
  sz->loaded = false;

  return sz;
}

static struct spt_file *new_file_spt_entry(struct file *f, void *uva, off_t offset,
    size_t prb, size_t pzb, bool writeable, enum spt_type type)
{
  struct spt_file *sf = malloc(sizeof(struct spt_file));

  if (sf == NULL)
    return sf;

  sf->vaddr = uva;
  sf->offset = offset;
  sf->read_bytes = prb;
  sf->zero_bytes = pzb;
  sf->writeable = writeable;
  sf->type = type;
  sf->file = f;
  sf->loaded = false;

  return sf;
}

/* find an entry in spt_table */
struct spt_general *find_lazy_page_spt_entry(struct thread *t, uint32_t *vaddr)
{
  struct hash_elem *e;
  struct spt_general sg;

  sg.vaddr = vaddr;
  lock_acquire(&t->spt_table_lock);
  e = hash_find(&t->spt_table, &sg.spt_hash_elem);
  lock_release(&t->spt_table_lock);

  if (e == NULL) 
  {
    printf("vaddr 0x%x in %s not founded....\n", vaddr, t->name);
    return NULL;
  }
  else 
    return hash_entry(e, struct spt_general, spt_hash_elem);
}

/* laod a page */
bool load_lazy_page_spt_entry(struct spt_general *sg)
{
  if (sg == NULL)
    return false;

  
  switch(sg->type)
  {
    case MMF:
    case FILE:
      return load_file_page(sg);
    case ZERO:
      return load_zero_page(sg);
    case SWAP:
      return load_swap_page(sg);
    default:
      return false;
  }

  return true;
}

static bool load_swap_page(struct spt_general *sg)
{
  bool status;
  struct spt_swap *ss = sg;
  void *f = frame_get_page(PAL_USER, sg->vaddr);
  struct thread *t = thread_current();
  printf("%d load swap page...0x%x\n", t->tid, f);

  status  = swap_out(ss->idx, f);
  if (!status)
    PANIC("swap out error");

  pagedir_set_page(t->pagedir, ss->vaddr, f, ss->writeable);
  ss->loaded = true;
  return status;
}

static bool load_file_page(struct spt_general *sg)
{
  struct spt_file *sf = sg;
  void *f = frame_get_page(PAL_USER, sg->vaddr);
  struct thread *t = thread_current();

  /* loading */
  file_seek(sf->file, sf->offset);
//  printf("offset: %d read_bytes: %d zero_bytes: %d\n", 
//      sf->offset, sf->read_bytes, sf->zero_bytes);
  if (f == NULL)
  {
    PANIC("memory");
  }
//  if (sf->type == MMF)
//    printf("sf->vaddr 0x%x read_bytes %d, zero_bytes%d\n ", 
//        sf->vaddr, sf->read_bytes, sf->zero_bytes);

  if (file_read(sf->file, f, sf->read_bytes) != sf->read_bytes)
  {
    frame_free_page(f);
    return false;
  }

  memset(f + sf->read_bytes, 0, sf->zero_bytes);
  if (pagedir_get_page(t->pagedir, sf->vaddr) != NULL)
  {
    frame_free_page(f);
    return false;
  }

  if (pagedir_set_page(t->pagedir, sf->vaddr, f, sf->writeable))
  {
    sf->loaded = true;
  } else
    return false;

  return true;
}

static bool load_zero_page(struct spt_general *sg)
{
  struct spt_zero *sz = sg;
  void *f = frame_get_page(PAL_USER | PAL_ZERO, sz->vaddr);
  struct thread *t = thread_current();


  printf("load zero page...\n");
  if (f == NULL)
  {
    PANIC("memory");
  }

  if (pagedir_get_page(t->pagedir, sz->vaddr) != NULL)
  {
    frame_free_page(f);
    return false;
  }

  if (pagedir_set_page(t->pagedir, sz->vaddr, f, true))
  {
    sz->loaded = true;
  } else 
    return false;

  return true;
}

static void spt_hash_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
  struct spt_general *sg = hash_entry(e, struct spt_general, spt_hash_elem);

  switch(sg->type) 
  {
    case SWAP:
    case MMF :
    case FILE:
      free((struct file *)sg);
      break;
    case ZERO:
      free((struct zero *)sg);
      break;
    default:
      break;
  }
}

bool destory_spt_table(struct thread *t)
{
  if (hash_empty(&t->spt_table))
  {
    hash_destroy(&t->spt_table, NULL);
    return true;
  }
 // printf("hash table is not empty[size : %d]\n", hash_size(&t->spt_table));

  hash_destroy(&t->spt_table, spt_hash_destroy_func);
  if (!hash_empty(&t->spt_table))
    printf("still not empty\n");

  return true;
}

void print_spt_table(struct thread *t)
{
  struct hash_iterator hi;

  printf("-- spt table --\n");
  if (hash_empty(&t->spt_table))
  {
    printf("empty spt table....\n");
    return;
  }
  hash_first(&hi, &t->spt_table);
  while (hash_next(&hi))
  {
    struct spt_file * sg = hash_entry(hash_cur(&hi), struct spt_file, spt_hash_elem);
    if (sg->type == FILE || sg->type == MMF)
      printf("type : MMF | FILE\n");
    else if (sg->type == SWAP)
      printf("type : SWAP\n");
    else if (sg->type == ZERO)
      printf("type : ZERO vaddr : 0x%8x\n", sg->vaddr);
    if (sg->type == FILE || sg->type == MMF)
    {
      printf(" - vaddr : 0x%8x read_bytes: %8d zero_bytes: %8d\n", 
          sg->vaddr, sg->read_bytes, sg->zero_bytes);
    }
  }
  printf("-- end --\n");
}
