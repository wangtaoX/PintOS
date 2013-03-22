#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "devices/block.h"

/* four types of spt entry */
enum spt_type
{
  SWAP = 1,
  FILE = 2,
  MMF = 4,
  ZERO = 8,
};

/* this is the geneal struct of spt, the members of each
 * concrete struct must begin with it */
struct spt_general
{
  uint32_t *vaddr;
  struct hash_elem spt_hash_elem;
  enum spt_type type;
};

struct spt_file
{
  uint32_t *vaddr;
  struct hash_elem spt_hash_elem;
  enum spt_type type;
  
  /* page in disk file */
  struct file *file;
  off_t offset;
  int32_t read_bytes;
  int32_t zero_bytes; /* may not need when mmf */
  bool writeable;
  bool loaded;
};

struct spt_zero
{
  uint32_t *vaddr;
  struct hash_elem spt_hash_elem;
  enum spt_type type;

  /* all zero page nothing needed */
  bool writeable;
  bool loaded;
};

struct spt_swap
{ 
  /* currently empty */
  uint32_t *vaddr;
  struct hash_elem spt_hash_elem;
  enum spt_type type;

  size_t idx;
  bool loaded;
  bool writeable;
};

void print_spt_table(struct thread *t);                   /* debug purpose */
void init_spt_table(struct thread *t);
bool destory_spt_table(struct thread *t);
void add_spt_entry(struct thread *t, struct spt_general *sg);
struct spt_general *new_spt_entry(struct file *f, void *uva, off_t offset,
    size_t prb, size_t pzb, bool writeable, enum spt_type type);

struct spt_general *find_lazy_page_spt_entry(struct thread *t, uint32_t *vaddr);
bool load_lazy_page_spt_entry(struct spt_general *sg);
struct spt_swap * new_swap_spt_entry(void *uva, size_t index);
#endif
