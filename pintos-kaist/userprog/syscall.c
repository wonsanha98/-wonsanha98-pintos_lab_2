#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call. 
이전에는 시스템 콜 서비스가 인터럽트 핸들러에 의해 처리되었다
(예: 리눅스에서 int 0x80). 그러나 x86-64에서는 제조사가
시스템 콜을 요청하기 위한 효율적인 경로인 syscall 명령어를 제공한다.

syscall 명령어는 모델 특수 레지스터(MSR)의 값을 읽어서 동작한다.
자세한 내용은 매뉴얼을 참조하라. */

#define MSR_STAR 0xc0000081         /* Segment selector MSR은 syscall 명령어가 동작할 때 
									   참조하는 모델 특수 레지스터(MSR)로
									   세그먼트 셀렉터 값을 포함한다. */
#define MSR_LSTAR 0xc0000082        /* long mode에서 SYSCALL 명령어가 호출될 때 제어가 이동하는 대상 주소를 의미한다. */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags를 마스킹하기 위한 값. */

void check_addr(const void *addr);
struct file *fd_tofile(int fd);


void halt (void);
void exit (int status);

pid_t pro_fork(const char *thread_name, struct intr_frame *_if);
int wait (pid_t pid);


bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

int open(const char *file);
int filesize(int fd);


int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);

void seek (int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);


void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* 인터럽트 서비스 루틴은 syscall_entry가 사용자 영역 스택을 
	   커널 모드 스택으로 교체할 때까지 어떤 인터럽트도 처리해서는 안 된다. 
	   따라서 우리는 FLAG_FL을 마스킹했다. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	
	lock_init(&filesys_lock);
}

/* 주요 시스템 콜 인터페이스.
 */
void
syscall_handler (struct intr_frame *f UNUSED) {

	int call_number = (int)f->R.rax;

	switch(call_number){
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit((int)f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = pro_fork((const char*) f->R.rdi, f);
			break;
		case SYS_EXEC:
			f->R.rax = exec((const char *) f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create((const char *)f->R.rdi, (unsigned)f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove((const char *)f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open((const char *)f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize((int)f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read((int)f->R.rdi, (void*)f->R.rsi, (unsigned)f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write((int)f->R.rdi, (const void*)f->R.rsi, (unsigned)f->R.rdx);
			break;
		case SYS_SEEK:
			seek((int)f->R.rdi, (unsigned)f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell((int)f->R.rdi);
			break;
		case SYS_CLOSE:
			close((int)f->R.rdi);
			break;
		default:
			thread_exit();
			break;
	}

}

void halt (void){
	power_off();
	return -1;
}


void exit (int status)
{
	struct thread *curr = thread_current();
	printf("%s: exit(%d)\n", curr->name, status); 
	thread_exit();
}

pid_t pro_fork (const char *thread_name, struct intr_frame *_if)
{	
	lock_acquire(&filesys_lock);
	pid_t p_fork = process_fork(thread_name, _if);
	lock_release(&filesys_lock);
	return p_fork;
}

int exec(const char *cmd_line)
{
	check_addr(cmd_line);
	int ex = process_exec(cmd_line);
	if(ex == -1)
	{
		return TID_ERROR;
	}
	return thread_current()->tid;
}


int wait(pid_t pid)
{
	lock_acquire(&filesys_lock);
	int w_it = process_wait(pid);
	lock_release(&filesys_lock);
	return w_it;
}


bool create(const char *file, unsigned initial_size)
{
	check_addr(file);

	lock_acquire(&filesys_lock);
	bool cre = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	return cre;
} 


bool remove(const char *file)
{
	check_addr(file);
	lock_acquire(&filesys_lock);
	bool rem = filesys_remove(file);
	lock_release(&filesys_lock);
	return rem;
}

int open(const char *file)
{
	check_addr(file);
	struct thread *curr = thread_current();

	int open_fd;
	bool is_not_full = false;
	for(open_fd = 2; open_fd < 64; open_fd++)
	{
		if(curr->fd_table[open_fd] == NULL)
		{
			is_not_full = true;
			break;
		}
	}
	if(!is_not_full) return -1;

	lock_acquire(&filesys_lock);
	struct file *open_file = filesys_open(file);
	lock_release(&filesys_lock);
	if(open_file == NULL) return -1;

	curr->fd_table[open_fd] = open_file;
	curr->fd = open_fd;

	return open_fd;
}


int filesize(int fd)
{
	lock_acquire(&filesys_lock);
	struct file *size_file = fd_tofile(fd);
	lock_release(&filesys_lock);

	if(size_file == NULL)
	{
		return -1;
	}

	return file_length(size_file);
}


int read(int fd, void *buffer, unsigned size)
{
	check_addr(buffer);
	check_addr(buffer + size - 1);

	if(size == 0)
	{
		return 0;
	}

	if(fd == 0)
	{	
		char *buf = (char *)buffer;
		lock_acquire(&filesys_lock);
		for(int i = 0; i < size; i++)
		{	
			buf[i] = input_getc();
		}
		lock_release(&filesys_lock);
		return size;
	}
	else if(fd == 1)
	{
		exit(-1);
	}
	else if(1 < fd < 64)
	{
		
		struct file *read_file = fd_tofile(fd);
		if(read_file == NULL)
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


int write(int fd, const void *buffer, unsigned size)
{
	check_addr(buffer);
	check_addr(buffer + size - 1);

	if(fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}
	else if(fd == 0)
	{
		return -1;
	}
	else if(1 < fd < 64)
	{
		struct file *write_file = fd_tofile(fd);
		if(write_file == NULL)
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

void seek (int fd, unsigned position)
{
	if(fd < 2)
	{
		return;
	}

	struct file *seek_file = fd_tofile(fd);
	check_addr(seek_file);	

	if(seek_file == NULL)
	{
		return;
	}
	lock_acquire(&filesys_lock);
	file_seek (seek_file, position);
	lock_release(&filesys_lock);
}

unsigned tell(int fd)
{
	if(fd < 2)
	{
		return;
	}

	if(fd < 0 || 64 <= fd)
	{
		return; 
	}

	struct file *tell_file = fd_tofile(fd);
	check_addr(tell_file);
	
	if(tell_file == NULL)
	{
		return;
	}
	lock_acquire(&filesys_lock);
	file_tell(tell_file);
	lock_release(&filesys_lock);
}


void close(int fd)
{
	struct thread *curr = thread_current();

	if(fd < 2)
	{
		return;
	}
	
	struct file *cl_file = fd_tofile(fd);
	// check_addr(cl_file);

	if(cl_file == NULL)
	{
		exit(-1);
	}

	if(fd < 0 || 64 <= fd)
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
	if(addr == NULL) exit(-1);
	if(!is_user_vaddr(addr)) exit(-1);
	if(pml4_get_page(thread_current()->pml4, addr) == NULL) exit(-1);
}

struct file *fd_tofile(int fd)
{
	if(fd < 0 || 64 <= fd)
	{
		return NULL;
	}

	struct thread *curr = thread_current();
	struct file *file = curr->fd_table[fd];
	return file; 
}