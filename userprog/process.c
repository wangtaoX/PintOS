# include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/timer.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

static bool load_segment_lazily(struct file *file, off_t offset,
    void *uva, uint32_t read_bytes, uint32_t zero_bytes, bool writeable);
/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  /* #### Parse the option */
  char *save_ptr;
  char *file_name_on_stack;
  /* Name of this user procsee */
  char *file_name_;
  struct thread *child_process;
  /* #### */
  

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */

  //file_name_on_stack = malloc(strlen(file_name));
  file_name_on_stack = palloc_get_page(0);
  //memcpy(file_name_on_stack, file_name, strlen(file_name) + 1);
  memcpy(file_name_on_stack, file_name, PGSIZE);

  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  memcpy(fn_copy, file_name, PGSIZE);
  
  /* #### */
  file_name_ = strtok_r(file_name_on_stack, " ", &save_ptr);
  /* #### */
  
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name_, PRI_DEFAULT, start_process, fn_copy);
  /* free it */
  palloc_free_page(file_name_on_stack);
#ifdef USERPROG
  /* Parent process cannot return from the EXEC until it knows whether 
   * the child process successfully loaded its executable*/
  //printf("tid %d create tid %d\n", thread_current()->tid, tid);
  if (tid != TID_ERROR)
  {
    child_process = thread_find_by_tid(tid);
    if (child_process != NULL)
    {
      sema_down(&child_process->wait_sema);
      sema_init(&child_process->wait_sema, 0);
      if (child_process->exit_status == -1)
        //printf("child failed\n");
        tid = TID_ERROR;
    }
    else
      tid = TID_ERROR;
  }
#endif

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  /* #### */
  char *save_ptr;
  char *argv[DEFAULT_ARGS];
  char *token;
  int argc = 0;
  uint8_t *base = PHYS_BASE;
  int i, total_byte = 0, len;

  for (token = strtok_r(file_name, " ", &save_ptr); token != NULL;
      token = strtok_r(NULL, " ", &save_ptr))
    argv[argc++] = token;

  //for (i = 0; i<argc; i++)
  //  printf("ARGV[%d] %s\n", i,argv[i]);
  /* #### */


  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (argv[0], &if_.eip, &if_.esp);

  /* If load failed, quit. */
  if (!success) 
  {
    thread_current()->exit_status = -1;
    sema_up(&(thread_current()->wait_sema));
    thread_yield();
    thread_exit ();
  }
  else
  {
  /* Weap up the parent process, it is waiting on EXEC */
    sema_up(&(thread_current()->wait_sema));
    thread_yield();
  }

  /* #### Here, arguments passing, now *esp equal to PHYS_BASE */
  //printf("argc : %d Origin base:%x\n", argc, base);

  /* Push argv[] on stack */
  for (i = argc - 1; i>=0; i--)
  {
    len = strnlen(argv[i], 14);
    base = base - len - 1;
    //printf("argv[%d] : %s len : %d\n", i, argv[i], len);
    memcpy((void *)base, (void *)argv[i], len+1);
    //printf(" base argv[%d] %s\n", i, (char *)base);
    argv[i] = base;
    total_byte = total_byte + len + 1;
  }
  //printf("STring total_byte : %d Base : %x\n", total_byte, base);

  /* Align in 4 bytes */
  total_byte = 4 - total_byte % 4;
  base = base - total_byte;
  memset(base, 0, total_byte);
  
  //printf("Align STring Base : %x\n", base);
  
  /* Set argv[argv] to NULL */
  base = memset(base - 4, 0, 4);
  //printf("Argv[%d] base : %x\n",argc, base);

  /* Push argv[i] on stack (pointer) */
  for (i = argc - 1; i>=0; i--)
  {
    base = base - 4;
    *(uint32_t *)base = argv[i];
    //printf("*base : %x\n", *(uint32_t *)base);
    //printf("Argv[%d] base  : %x &Argv[%d] : %x\n", i, base, i, argv[i]);
  }
  /* Push argv on stack */
  base = base - 4;
  *(uint8_t **)base = base + 4;
  /* Push argc on stack */
  base = base - 4;
  *(int32_t *)base = argc;
  //printf("Argc Base : %x\n", base);
  /* Push a fake return address on stack */
  base = memset(base-4, 0, 4);

  if_.esp = base;
  //printf("base : %x\n", base);
  //hex_dump(base, (void *)base, 128, true);
  
  palloc_free_page(file_name_);
  /* #### */

  print_spt_table(thread_current());
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *c;
  int status = -1;

  c = thread_find_by_tid(child_tid);

  //if (c == NULL)
  //  printf("Find No child\n");

  if (c != NULL)
  {
    //printf("c->is_waited %d\n", c->is_waited);
    if (c->is_waited != true)
    {
      c->is_waited = true;
      //printf("%s in process wait\n",thread_current()->name);
      sema_down(&c->wait_sema);
      status = c->exit_status;
      //printf("UP%s in process wait : status %d\n",thread_current()->name, status);
    }
    else
      status = -1;
  }
  //printf("%s in process wait : status %d\n",thread_current()->name, status);

  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  struct list_elem *e;
  struct thread *child;
  int i = 3;

  /* Parent process exit before its child process, change every child
   * process`s parent to NULL */
  for (e = list_begin(&cur->child_list); e != list_end(&cur->child_list);
      e = list_next(e))
  {
    child = list_entry(e, struct thread, child_elem);
    child->parent_process = NULL;
  }

  /* When load failure, remove it from parent`s child_list */
  if (cur->parent_process != NULL)
    list_remove(&(cur->child_elem));

  while(is_not_main(cur) && i < DEFAULT_OPEN_FILES)
  {
    if ((cur->fd)[i] != NULL)
      file_close((cur->fd)[i]);
    i++;
  }
  free(cur->fd);

  destory_spt_table(cur);
  /* ### Sema up the parent which is waiting on this child process */
  if (!list_empty(&(thread_current()->wait_sema.waiters)))
  {
    sema_up(&(thread_current()->wait_sema));
    thread_yield();
  }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);

  /*### An executable file cannot be modified */
  if (file != NULL && !file->deny_write)
    file_deny_write(file);
  /*### */
  //printf("0x%x file->deny_write : %d\n", file, file->deny_write);
  
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;

              if (mem_page == 0)
                mem_page += 0x1000;

              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment_lazily (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

  //debug_frame_table();
 done:
  /* We arrive here whether the load is successful or not. */
  //file_close (file);
  t->fd[DEFAULT_OPEN_FILES - 1] = file;
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  //if (phdr->p_vaddr < PGSIZE)
  //  return false;
  if (phdr->p_offset < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      //uint8_t *kpage = palloc_get_page (PAL_USER);
      uint8_t *kpage = frame_get_page(PAL_USER, upage);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          //palloc_free_page (kpage);
          frame_free_page(kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          //palloc_free_page (kpage);
          frame_free_page(kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/*####*/
/* lazy loading of segment */
static bool load_segment_lazily(struct file *file, off_t offset,
    void *uva, uint32_t read_bytes, uint32_t zero_bytes, bool writeable)
{
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(uva) == 0);
  ASSERT(offset % PGSIZE == 0);

//  printf("load lazily start [segment]\n");
  while(read_bytes > 0 || zero_bytes > 0)
  {
    /* page_read_bytes plus page_zero_bytes must be equal to PGSIZE */
    size_t page_read_bytes = read_bytes >= PGSIZE ? PGSIZE : read_bytes;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    struct spt_general *sg;
    enum spt_type type;
    struct thread *t = thread_current();
    
    type = (page_zero_bytes == PGSIZE) ? ZERO : FILE;

    /* return a new spt entry  */
    sg = new_spt_entry(file, uva, offset, page_read_bytes, page_zero_bytes, writeable, type); 
    if (sg == NULL)
      return false;

//    printf("read_bytes : %d zero_bytes : %d\n", page_read_bytes, page_zero_bytes);
    add_spt_entry(t, sg);

    /* update the value */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    uva += PGSIZE;
    offset += PGSIZE;
  }
  file_seek (file, offset);

//  printf("load lazily end\n");

  return true;
}
/*#### */
/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;
  struct spt_zero *sz;
  struct thread *t = thread_current();

  //kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  kpage = frame_get_page (PAL_USER | PAL_ZERO, (uint8_t *)PHYS_BASE - PGSIZE);
  sz = new_spt_entry(NULL, (void *)PHYS_BASE - PGSIZE, 0, 0, 0, true, ZERO);
  add_spt_entry(t, sz);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        frame_free_page (kpage);
        //palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
