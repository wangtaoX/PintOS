#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"

typedef int pid_t;

static void syscall_handler (struct intr_frame *);

static void _do_sys_halt(void);
static void _do_sys_exit(int status);
static pid_t _do_sys_exec(const char *cmd_line);
static int _do_sys_wait(pid_t pid);
static bool _do_sys_create(const char *file, unsigned initial_size);
static bool _do_sys_remove(const char *file);

/* Reads a byte at user virtual address UADDAR.
 * UADDAR must be below PHYS_BASE, Return the 
 * byte value if successful, -1 if a segfault occurred*/
static int 
get_user(const uint8_t *uaddr)
{
  int result;

  asm("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));

  return result;
}
/* Write bytes to user address UDST */
static bool 
put_user(uint8_t *udst, uint8_t byte)
{
  int error_code;

  asm("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a" (error_code), "=m" (*udst) : "q" (byte));

  return error_code != -1;
}
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *esp = (uint32_t *)(f->esp);
  
  if ((void *)esp >= PHYS_BASE || (void *)esp == NULL 
      || (void *)esp < (void *)0x08048000 
      || (void *)(esp + 4) >= PHYS_BASE
      || !is_mapped_addr(thread_current()->pagedir, esp)
      || !is_mapped_addr(thread_current()->pagedir, esp + 4))
  {
    printf("%s: exit(%d)\n", thread_current()->name, -1);
    thread_current()->exit_status = -1;
    thread_exit();
  }

  switch (*esp)
  {
    case SYS_HALT:
      _do_sys_halt();
      break;
    case SYS_EXIT:
      _do_sys_exit(*(int *)(esp + 1));
      break;
    case SYS_WAIT:
      f->eax = _do_sys_wait(*(pid_t *)(f->esp + 1));
      break;
    case SYS_EXEC:
      f->eax = _do_sys_exec((char *)(*(esp + 1)));
      break;
    case SYS_CREATE:
      f->eax = _do_sys_create((char *)(*(esp + 4)), *(unsigned *)(esp + 2));
      break;
    case SYS_REMOVE:
      f->eax = _do_sys_remove((char *)(*(esp + 1)));
  }
}

static void _do_sys_halt()
{
  printf("_do_sys_halt\n");
  shutdown();
}

static void _do_sys_exit(int status)
{
  struct thread *t = thread_current();

  t->exit_status = status;
  printf("%s: exit(%d)\n", thread_current()->name, t->exit_status);

  thread_exit();
}
static int _do_sys_wait(pid_t pid)
{

  return process_wait(pid);
}

static pid_t _do_sys_exec(const char *cmd_line)
{
  return process_execute(cmd_line);
}

static bool _do_sys_create(const char *file, unsigned initial_size)
{
  if (file != NULL)
    return filesys_create(file, initial_size);
  else
    return false;
}
static bool _do_sys_remove(const char *file)
{
  if (file != NULL)
    return filesys_remove(file);
  else
    return false;
}
