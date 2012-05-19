#include "userprog/syscall.h"
#include "devices/input.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <console.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

typedef int pid_t;

static void syscall_handler (struct intr_frame *);

static void _do_sys_halt(void);
static void _do_sys_exit(int status);
static pid_t _do_sys_exec(const char *cmd_line);
static int _do_sys_wait(pid_t pid);
static bool _do_sys_create(const char *file, unsigned initial_size);
static bool _do_sys_remove(const char *file);
static int _do_sys_open(const char *file);
static void _do_sys_close(int fd);
static int _do_sys_filesize(int fd);
static unsigned _do_sys_tell(int fd);
static int _do_sys_read(int fd, void *buffer, unsigned size);
static int _do_sys_write(int fd, void *buffer, unsigned size);
static void _do_sys_seek(int fd, unsigned position);

/* Check out whether the UADDR is a valid user virtual address */
static bool _valid_uaddr(void *uaddr)
{
  struct thread *cur = thread_current();

  if (!is_mapped_addr(cur->pagedir, uaddr) || !is_user_vaddr(uaddr)
      || uaddr < (void *)0x08048000 || uaddr == NULL)
    return false;
  else
    return true;
}
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
  
  if (!_valid_uaddr((void *)esp) || !_valid_uaddr((void *)(esp + 4)))
  {
    printf("%s: exit(%d)\n", thread_current()->name, -1);
    thread_current()->exit_status = -1;
    thread_exit();
  }

  //printf("SYS_NUMBER : %d\n", *(int *)esp);
//  printf("size %d fd %d size %d\n", *(int32_t *)(esp + 3), *(int32_t *)(esp + 2), *(int32_t *)(esp + 1));

  switch (*esp)
  {
    case SYS_HALT:
      _do_sys_halt();
      break;
    case SYS_EXIT:
      _do_sys_exit(*(int *)(esp + 1));
      break;
    case SYS_WAIT:
      f->eax = _do_sys_wait(*(pid_t *)(esp + 1));
      break;
    case SYS_EXEC:
      if (!_valid_uaddr((void *)(*(esp + 1))))
        goto done;
      f->eax = _do_sys_exec((char *)(*(esp + 1)));
      break;
    case SYS_CREATE:
      if (!_valid_uaddr((void *)(*(esp + 4))))
        goto done;
      f->eax = _do_sys_create((char *)(*(esp + 4)), *(unsigned *)(esp + 2));
      break;
    case SYS_REMOVE:
      if (!_valid_uaddr((void *)(*(esp + 1))))
        goto done;
      f->eax = _do_sys_remove((char *)(*(esp + 1)));
      break;
    case SYS_OPEN:
      if (!_valid_uaddr((void *)(*(esp + 1))))
        goto done;
      f->eax = _do_sys_open((char *)(*(esp + 1)));
      break;
    case SYS_CLOSE:
      _do_sys_close(*(int *)(esp + 1));
      break;
    case SYS_FILESIZE:
      f->eax = _do_sys_filesize(*(int *)(esp + 1));
      break;
    case SYS_TELL:
      f->eax = _do_sys_tell(*(int *)(esp + 1));
      break;
    case SYS_READ:
      if (!_valid_uaddr((void *)(*(esp + 6))))
        goto done;
      f->eax = _do_sys_read(*(int *)(esp + 2), (char *)(*(esp + 6)), 
          *(int *)(esp + 3));
      break;
    case SYS_WRITE:
      if (!_valid_uaddr((void *)(*(esp + 6))))
        goto done;
      f->eax = _do_sys_write(*(int *)(esp + 2), (char *)(*(esp + 6)), 
          *(int *)(esp + 3));
      break;
    case SYS_SEEK:
      _do_sys_seek(*(int *)(esp + 4), *(uint32_t *)(esp + 2));
      break;
  }

  return ;
  done:
    printf("%s: exit(%d)\n", thread_current()->name, -1);
    thread_current()->exit_status = -1;
    thread_exit();
}

static void _do_sys_halt()
{
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
static int _do_sys_open(const char *file)
{
  struct thread *cur = thread_current();
  int fd = 2;
  struct file *f;

  f = filesys_open(file);

  if (f != NULL)
  {
    while(cur->fd[++fd] != NULL);
    cur->fd[fd] = f;
  }
  else
    fd = -1;
  
  return fd;
}
static void _do_sys_close(int fd)
{
  struct thread *cur = thread_current();

  if (fd < 0 || fd > DEFAULT_OPEN_FILES)
    return ;

  file_close((cur->fd)[fd]);
  (cur->fd)[fd] = NULL;
}

static int _do_sys_filesize(int fd)
{
  struct thread *cur = thread_current();

  if (fd < 0 || (cur->fd[fd]) == NULL)
    return -1;

  return file_length((cur->fd)[fd]);
}

static unsigned _do_sys_tell(int fd)
{
  struct thread *cur = thread_current();

  if (fd < 0 || (cur->fd)[fd] == NULL)
    return -1;

  /* Rerutn the position of the next byte to be read or written 
   * in open file FD */
  return file_tell((cur->fd)[fd]) + 1;
}
static int _do_sys_read(int fd, void *buffer, unsigned size)
{
  struct thread *cur = thread_current();
  uint8_t *buffers = buffer;
  unsigned read_size = 0;
  struct file *f;

  //printf("_do_sys_read 0x%x\n", buffer);
  if (fd < 0 || fd > DEFAULT_OPEN_FILES || 
      (fd > 2 && (cur->fd)[fd] == NULL) || fd == 1)
    return -1;


  if (size == 0)
    return 0;

  if (fd == 0)
  {
    *buffers = input_getc();
    buffers++;
    read_size++;
  }
  else 
  {
    f = (struct file *)(cur->fd)[fd];
    read_size = file_read(f, buffer, size);
  }

  //printf("buffer : %s\n", (char *)buffer);

  return read_size;
}

static int _do_sys_write(int fd, void *buffer, unsigned size)
{
  struct thread *cur = thread_current();
  int write_size = 0;

  //printf("_do_sys_write FD : %x\n", fd);
  if (fd <= 0 || fd > DEFAULT_OPEN_FILES ||
      (fd > 2 && (cur->fd)[fd] == NULL))
    return -1;

  if (size == 0)
    return 0;

  if (fd == 1)
  {
    putbuf(buffer, size);
    write_size = size;
  }
  else
  {
    write_size = file_write((cur->fd)[fd], buffer, size);
  }

  return write_size;
}
static void _do_sys_seek(int fd, unsigned position)
{
  struct thread *cur = thread_current();

  if (fd < 0 || (cur->fd)[fd] == NULL)
    return ;

  (cur->fd)[fd]->pos = position;

  return ;
}
