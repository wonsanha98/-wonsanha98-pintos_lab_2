#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

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

void halt (void);
void exit (int status);

bool create(const char *name, unsigned initial_size);
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
			break;
		case SYS_EXEC:
			break;
		case SYS_WAIT:
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
	// curr->status = status;
	printf("%s: exit(%d)\n", curr->name, status); 
	thread_exit();
}



bool create(const char *name, unsigned initial_size)
{
	if(name == NULL)
	{
		exit(-1);
	}

	if(initial_size == 0)
	{
		false;
	}
	return filesys_create(name, initial_size);
} 

bool remove(const char *file)
{
	if(file == NULL)
	{
		exit(-1);
	}
	return filesys_remove(file);
}

int open(const char *file)
{
	if(file == NULL)
	{
		exit(-1);
	}
	struct file *open_file = filesys_open(file);
	if(open_file == NULL)
	{
		return -1;
	}
	return (int)open_file;
}

int filesize(int fd)
{
	return file_length ((struct file *)fd);
}


int read(int fd, void *buffer, unsigned size)
{
	if(fd == 0)
	{
		input_getc();
	}
	return -1;
	// else
	// {
	// 	file_read();
	// }
}


int write(int fd, const void *buffer, unsigned size)
{
	// if(fd == 0)

	if(fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}
	return -1;
	// else
	// {
	// 	file_write();
	// }
}




void seek (int fd, unsigned position)
{
	if(fd == NULL)
	{
		return;
	}
	file_seek ((struct file*)fd, position);
}

unsigned tell(int fd)
{
	if(fd == NULL)
	{
		return;
	}
	file_tell((struct file*)fd);
}


void close(int fd)
{
	if(fd < 2)
	{
		return;
	}

	if(fd == NULL)
	{
		exit(-1);
	}

	file_close((struct file*)fd);
}

