#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* initd 및 기타 프로세스를 위한 일반적인 프로세스 초기화 함수 */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* FILE_NAME에서 로드된 "initd"라는 첫 번째 유저랜드 프로그램을 시작한다.
 * 새 스레드는 스케줄될 수 있으며(실행 상태가 될 수 있고), 심지어 종료될 수도 있다.
 * process_create_initd()가 반환되기 전에 새 스레드가 스케줄되거나 종료될 수 있다.
 * 스레드 ID를 반환하며, 스레드를 생성할 수 없을 경우 TID_ERROR를 반환한다.
 * 이 함수는 한 번만 호출되어야 한다는 점에 유의하라.*/
//Create process and thread 
tid_t
process_create_initd (const char *file_name) { //run_task에서 파라미터에 
	char *fn_copy;	//파싱하지 않은 인자가 들어온다.
	tid_t tid;

	/* FILE_NAME의 복사본을 생성하라
	 * 그렇지 않으면 호출자와 load() 함수 사이에 경쟁 상태(race condition)가 발생할 수 있다. */
	fn_copy = palloc_get_page (0); //하나의 사용 가능한 페이지를 확보하고 해당 페이지의 커널 가상 주소를 반환한다.
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);	//fn_copy(버퍼), file_name(복사할 str), PGSIZE(버퍼의 사이즈) 

	/* FILE_NAME을 실행할 새로운 스레드를 생성하라 */
	//initd 를 실행하는 스레드를 생성

	char *token;
	char *strl;
	token = strtok_r (file_name, " ", &strl);
	
	tid = thread_create (token, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);			//page 할당을 해제한다.
	return tid;								//스레드 id를 반환한다.
}

/* 첫 번째 사용자 프로세스를 실행하는 스레드 함수. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* 현재 프로세스를 name으로 복제한다. 새 프로세스의 스레드 ID를 반환하며,
스레드를 생성할 수 없으면 TID_ERROR를 반환한다.~ */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* 현재 스레드를 새로운 스레드로 복제한다.*/
	struct thread *curr = thread_current();
	memcpy(&curr->_if, if_, sizeof(struct intr_frame));
	//if_ 복사 성공
	int tid_t = thread_create (name, PRI_DEFAULT, __do_fork, thread_current ());	
	
	if(tid_t == -1)
	{
		return TID_ERROR;
	}
	else
	{
		return tid_t;
	}
	}  

#ifndef VM
/* 이 함수를 `pml4_for_each`에 전달하여 부모 프로세스의 주소 공간을 복제합니다.
 * 이 기능은 프로젝트 2에서만 사용됩니다. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;
	/* 1. TODO: parent_page가 커널 페이지인 경우, 즉시 반환하세요. */

	if(!is_user_vaddr(va))
	{
		return true;
	}

	/* 2. 부모의 페이지 맵 레벨 4(PML4)에서 VA(가상 주소)를 해석합니다. */
	parent_page = pml4_get_page (parent->pml4, va);

	if(parent_page == NULL)
	{	
		return false;
	}

	newpage = palloc_get_page(PAL_USER);

	/* 3. TODO: 자식 프로세스를 위해 새로운 PAL_USER 페이지를 할당하고, 결과를 변수에 설정하세요.
	 *    TODO: NEWPAGE. */
	/* TODO: 부모의 페이지를 새 페이지로 복사하고,
	 * TODO: 부모의 페이지가 쓰기 가능한지 확인한 후,
 	 * TODO: 그 결과에 따라 WRITABLE 플래그를 설정하세요.*/
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	// #define is_writable(pte) (*(pte) & PTE_W) 참고용

	/* 5. 자식의 페이지 테이블에 주소 VA에 해당하는 새 페이지를
	 *    WRITABLE 권한으로 추가하세요. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
	/* 6. TODO: 페이지 삽입에 실패한 경우, 에러 처리를 수행하세요. */
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* 부모의 실행 컨텍스트를 복사하는 스레드 함수.
힌트) parent->tf는 프로세스의 사용자 영역 컨텍스트를 포함하고 있지 않다.
즉, 이 함수에 process_fork의 두 번째 인자를 전달해야 한다. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: 어떻게든 parent_if를 통과해야 한다. (즉, process_fork() 함수의 if_를) */
	struct intr_frame *parent_if = &parent->_if;
	bool succ = true;

	/* 1. CPU 컨텍스트를 로컬 스택으로 읽어온다. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. 페이지 테이블(PT)을 복제한다. */
	//페이지 테이블 복제부분이 없음
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

/* TODO: 여기에 여러분의 코드를 작성하세요.
 * TODO: 힌트) 파일 객체를 복제하려면 include/filesys/file.h에 정의된
 * TODO:       `file_duplicate` 함수를 사용하세요.
 * TODO:       주의할 점은, 부모 프로세스는 이 함수가
 * TODO:       부모의 자원을 성공적으로 복제하기 전까지
 * TODO:       fork()로부터 반환되어서는 안 됩니다.
 */
	for(int i = 0; i < 64; i++)
	{
		if(parent->fd_table[i] != NULL)
		{
			struct file *new_file = file_duplicate(parent->fd_table[i]);
			if(new_file == NULL)
			{
				succ = false;
			}
			current->fd_table[i] = new_file;
		}
	}

	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	thread_exit ();
}



/* 현재 실행 컨텍스트를 f_name으로 전환한다.
 * Returns -1 on fail. */
int
process_exec (void *f_name) { //실행하려는 바이너리 파일의 이름?
	char *file_name = palloc_get_page(PAL_ZERO);
	if(file_name == NULL) return -1;

	strlcpy(file_name, (char *)f_name, PGSIZE);
	bool success;

	/* 현재 실행 컨텍스트를 f_name으로 전환한다.
	 * 이는 현재 스레드가 다시 스케줄될 때 발생하기 때문이다,
	 * 실행 정보를 해당 멤버 변수에 저장하기 때문이다.. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;	//유저 데이터 세그먼트?
	_if.cs = SEL_UCSEG;						//유저 코드 세그먼트
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* 먼저 현재 컨텍스트를 종료(kill)한다. */
	process_cleanup ();

	/* 그 다음에 바이너리를 로드한다. */
	char *token;
	char *strl;
	token = strtok_r (file_name, " ", &strl);


	success = load (file_name, &_if);


	char *argv[99];
	int argc = 0;
	while (token != NULL) {
		argv[argc] = token;
		argc++;
		token = strtok_r(NULL, " ", &strl);  // 다음 호출
	}
	argv[argc] = NULL;
	_if.R.rdi = argc;   // USER_STACK
    

    char *str[99];

    for(int i = 0; i < argc; i++){
		uintptr_t b = strlen(argv[i])+1;
		_if.rsp = _if.rsp - b;
       	str[i] = _if.rsp;
		memcpy(str[i], argv[i], b);
	}

    uintptr_t num = _if.rsp;
    _if.rsp = _if.rsp & ~0x7;
   	uint8_t check_num = num - (_if.rsp);
   	memset(_if.rsp, (uint8_t)0 , check_num);
 
    _if.rsp = _if.rsp - 0x8;
	memset((char *)_if.rsp, 0 , 8);
    
	char **str2;
    for(int i = argc-1; i >= 0; i--){
		_if.rsp = _if.rsp - 0x8;
        str2 = _if.rsp;
		memcpy(str2, &str[i], 8);
	}

	_if.R.rsi = (uint64_t)_if.rsp;


    _if.rsp = _if.rsp - 0x8;
	memset((char *)_if.rsp, 0 , 8);


	/* 불로오기를 실패하면 프로그램을 종료한다. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/* 전환된 프로세스를 시작한다.. */
	do_iret (&_if); //실행할 레지스터를 잘 지정해야 한다?
	NOT_REACHED ();
}


/* TID에 해당하는 스레드가 종료될 때까지 기다린 후, 해당 스레드의 종료 상태(exit status)를 반환합니다.
만약 해당 스레드가 커널에 의해 종료되었다면(즉, 예외로 인해 강제 종료되었다면) -1을 반환합니다.
TID가 유효하지 않거나, 호출한 프로세스의 자식 프로세스가 아닌 경우, 
또는 이미 해당 TID에 대해 process_wait()이 성공적으로 호출된 적이 있다면,
기다리지 않고 즉시 -1을 반환합니다.
 *
 * 이 함수는 문제 2-2에서 구현될 예정입니다.
현재는 아무 작업도 수행하지 않습니다. */
int
process_wait (tid_t child_tid UNUSED) {
	//추천하는 방법은 process_wait를 구현하기 전에 
	//이곳에 무한 루프를 추가하는 것이다.
	for(long long i = 0; i < 2000000000; i++){
	}

	return -1;
}

/* 프로세스를 종료한다. 이 함수는 thread_exit()에 의해 호출된다. */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: 여기에 코드를 작성하세요.
	   TODO: 프로세스 종료 메시지를 구현하세요 (참고: project2/process_termination.html).
       TODO: 이곳에서 프로세스 자원 정리를 구현하는 것을 권장합니다. */	


	process_cleanup ();
}

/* 현재 프로세스의 자원을 해제(free) 한다. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* 현재 프로세스의 페이지 디렉터리를 파괴(destroy)하고, 이전 페이지 디렉터리로 전환한다.
	 * 커널 전용 페이지 디렉터리(kernel-only page directory)로 전환한다. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* 여기서 올바른 순서가 매우 중요하다. 반드시 다음을 설정해야 한다.
		 * 페이지 디렉터리를 전환하기 전에 cur->pagedir을 반드시 NULL로 설정해야 한다,
		 * 그렇지 않으면 타이머 인터럽트가 다시 이전 페이지 디렉터리로 전환하는 거를 방지하기 위해서
		 * 기본 페이지 디렉터리(base page directory)를 활성화해야 한다.
		 * 프로세스의 페이지 디렉터리를 파괴하기 전에 기본 
		 * 그렇지 않으면 현재 활성화된 페이지 디렉터리가 이미 파괴된 것이 될 수 있다.
		 * 그렇게 하지 않으면 이미 해제되고(clear) 페이지 디렉터리가 활성화된 상태가 되어버린다. */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* 실행 파일 헤터.  See [ELF1] 1-4 to 1-8.
 * 이것은 ELF 바인너리의 가장 앞부분에 나타난다. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* FILE_NAME에서 ELF 실행 파일을 현재 스레드로 로드한다.
 * 실행 파일의 진입점을 *RIP에 저장한다.
 * 그리고 초기 스택 포인터를 *RSP에 저장한다.
 * 성공하면 true를 반환하고, 그렇지 않으면 false를 반환한다. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();	//thread *t는 현재 running thread를 가리키는 포인터
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* 페이지 디렉터리를 할당하고 활성화한다. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());


	/* 실행 파일을 연다. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* 실행 파일의 헤더를 읽고 검증한다. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* 프로그램 헤더들을 읽는다. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* 이 세그먼트는 무시한다. 왜? */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* 스택을 초기화한다. */
	if (!setup_stack (if_))
		goto done;

	/* 시작 주소. */
	if_->rip = ehdr.e_entry;
	
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
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
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}


/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
