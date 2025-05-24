#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
/*------[ Project 2 System Call]------*/
// #include "threads/init.h"
#include "user/syscall.h"
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/*------[ Project 2 System Call]------*/
bool syscall_create(const char *file, unsigned initial_size);
int syscall_wait(pid_t pid);
pid_t syscall_fork(const char *thread_name, struct intr_frame *if_ UNUSED);
void syscall_exit(int status);
int syscall_write(int fd, const void *buffer, unsigned size);
void syscall_half(void);
bool syscall_remove(const char *file);
int syscall_open(const char *file);
int syscall_filesize(int fd);
int syscall_read(int fd, void *buffer, unsigned size);
void syscall_seek(int fd, unsigned position);
unsigned syscall_tell(int fd);
void syscall_close(int fd);

void check_addr(const void *addr);
struct file *fd_tofile(int fd);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

	void syscall_init(void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	uint64_t sys_number = f->R.rax;
	// printf("syscall Number%d\n", sys_number);
	switch (sys_number)
	{
	case SYS_HALT:
		syscall_half();
		break;
	case SYS_EXIT: 
		syscall_exit(f->R.rdi);
	break;
	case SYS_FORK:
		f->R.rax = syscall_fork((const char *)f->R.rdi, f);
		break;
	case SYS_EXEC: 
		break;
	case SYS_WAIT: 
		f->R.rax = syscall_wait((pid_t)f->R.rdi);
		break;
	case SYS_CREATE: 
		f->R.rax = syscall_create((const char *)f->R.rdi, (unsigned) f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = syscall_remove((const char *)f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = syscall_open((const char *)f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = syscall_filesize((int)f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = syscall_read((int)f->R.rdi, (void *)f->R.rsi, (unsigned)f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = syscall_write((int)f->R.rdi, (const void *)f->R.rsi, (unsigned)f->R.rdx);
		break;
	case SYS_SEEK:
		syscall_seek((int)f->R.rdi, (unsigned)f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = syscall_tell((int)f->R.rdi);
		break;
	case SYS_CLOSE:
		syscall_close((int)f->R.rdi);
		break;
	case SYS_MMAP:
		break;
	case SYS_MUNMAP:
		break;
	case SYS_CHDIR:
		break;
	case SYS_MKDIR:
		break;
	case SYS_READDIR:
		break;
	case SYS_ISDIR:
		break;
	case SYS_INUMBER:
		break;
	case SYS_SYMLINK:
		break;
	case SYS_DUP2:
		break;
	case SYS_MOUNT:
		break;
	case SYS_UMOUNT:
		break;
	default:
		thread_exit();
		break;
	}
}

bool syscall_create(const char *file, unsigned initial_size)
{
	check_addr(file);

	lock_acquire(&filesys_lock);
	bool cre = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	return cre;
}

int syscall_wait(pid_t pid)
{
	return process_wait(pid);
}

pid_t syscall_fork(const char *thread_name, struct intr_frame *if_ UNUSED)
{
	process_fork(thread_name, if_);
}

void syscall_exit(int status)
{
	struct thread* curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

int syscall_write(int fd, const void *buffer, unsigned size)
{
	check_addr(buffer);
	check_addr(buffer + size - 1);

	if (fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}
	else if (fd == 0)
	{
		return -1;
	}
	else if (1 < fd < 64)
	{
		struct file *write_file = fd_tofile(fd);
		if (write_file == NULL)
		{
			return 0;
		}
		else
		{
			lock_acquire(&filesys_lock);
			int wri = file_write(write_file, buffer, size);
			lock_release(&filesys_lock);
			return wri;
		}
	}
}

void syscall_half(void)
{
	power_off();
}

bool syscall_remove(const char *file)
{
	check_addr(file);
	lock_acquire(&filesys_lock);
	bool rem = filesys_remove(file);
	lock_release(&filesys_lock);
	return rem;
}

int syscall_open(const char *file)
{
	check_addr(file);
	struct thread *curr = thread_current();

	int open_fd;
	bool is_not_full = false;
	for (open_fd = 2; open_fd < 64; open_fd++)
	{
		if (curr->fd_table[open_fd] == NULL)
		{
			is_not_full = true;
			break;
		}
	}
	if (!is_not_full)
		return -1;

	lock_acquire(&filesys_lock);
	struct file *open_file = filesys_open(file);
	lock_release(&filesys_lock);
	if (open_file == NULL)
		return -1;

	curr->fd_table[open_fd] = open_file;
	curr->fd = open_fd;

	return open_fd;
}

int syscall_filesize(int fd)
{
	lock_acquire(&filesys_lock);
	struct file *size_file = fd_tofile(fd);
	lock_release(&filesys_lock);

	if (size_file == NULL)
	{
		return -1;
	}

	return file_length(size_file);
}

int syscall_read(int fd, void *buffer, unsigned size)
{
	check_addr(buffer);
	check_addr(buffer + size - 1);

	if (size == 0)
	{
		return 0;
	}

	if (fd == 0)
	{
		char *buf = (char *)buffer;
		lock_acquire(&filesys_lock);
		for (int i = 0; i < size; i++)
		{
			buf[i] = input_getc();
		}
		lock_release(&filesys_lock);
		return size;
	}
	else if (fd == 1)
	{
		syscall_exit(-1);
	}
	else if (1 < fd < 64)
	{

		struct file *read_file = fd_tofile(fd);
		if (read_file == NULL)
		{
			return 0;
		}
		else
		{
			lock_acquire(&filesys_lock);
			int rea = file_read(read_file, buffer, size);
			lock_release(&filesys_lock);
			return rea;
		}
	}
}

void syscall_seek(int fd, unsigned position)
{
	if (fd < 2)
	{
		return;
	}

	struct file *seek_file = fd_tofile(fd);
	check_addr(seek_file);

	if (seek_file == NULL)
	{
		return;
	}
	lock_acquire(&filesys_lock);
	file_seek(seek_file, position);
	lock_release(&filesys_lock);
}

unsigned syscall_tell(int fd)
{
	if (fd < 2)
	{
		return;
	}

	if (fd < 0 || 64 <= fd)
	{
		return;
	}

	struct file *tell_file = fd_tofile(fd);
	check_addr(tell_file);

	if (tell_file == NULL)
	{
		return;
	}
	lock_acquire(&filesys_lock);
	file_tell(tell_file);
	lock_release(&filesys_lock);
}

void syscall_close(int fd)
{
	struct thread *curr = thread_current();

	if (fd < 2)
	{
		return;
	}

	struct file *cl_file = fd_tofile(fd);
	// check_addr(cl_file);

	if (cl_file == NULL)
	{
		syscall_exit(-1);
	}

	if (fd < 0 || 64 <= fd)
	{
		return;
	}

	lock_acquire(&filesys_lock);
	file_close(cl_file);
	lock_release(&filesys_lock);
	curr->fd_table[fd] = NULL;
}



//////////////
void check_addr(const void *addr)
{
	if (addr == NULL)
		syscall_exit(-1);
	if (!is_user_vaddr(addr))
		syscall_exit(-1);
	if (pml4_get_page(thread_current()->pml4, addr) == NULL)
		syscall_exit(-1);
}

struct file *fd_tofile(int fd)
{
	if (fd < 0 || 64 <= fd)
	{
		return NULL;
	}

	struct thread *curr = thread_current();
	struct file *file = curr->fd_table[fd];
	return file;
}