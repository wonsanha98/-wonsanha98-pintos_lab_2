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
#include "userprog/syscall.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);
void hex_dump(uintptr_t ofs, const void *buf, size_t size, bool ascii);

/* General process initializer for initd and other process. */
static void
process_init(void)
{
	struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t process_create_initd(const char *file_name)
{
	char *fn_copy;
	char *token, *save_ptr;
	tid_t tid;
	// file_name : args-single onearg
	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy(fn_copy, file_name, PGSIZE);

	token = strtok_r(file_name, " ", &save_ptr);

	// token = strtok_r(fn_copy, " ", &save_ptr);
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(token, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
	{
		// syscall_exit(-1);
		palloc_free_page(fn_copy);
	}
	return tid;
}

/* A thread function that launches first user process. */
static void
initd(void *f_name)
{
#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif

	process_init();

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
	tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, if_);

	if (tid == -1)
	{
		// return TID_ERROR;
		syscall_exit(TID_ERROR);
	}

	struct thread *curr = thread_current();
	struct thread *child = NULL;
	struct list_elem *curr_elem = list_begin(&curr->children);
	int check_tid = -1;

	// 자식 tid 일치하는 스레드 찾아서 thread 반환하기.
	while (curr_elem != list_end(&curr->children))
	{
		child = list_entry(curr_elem, struct thread, ch_elem);
		check_tid = child->tid;
		if (check_tid == tid)
		{
			break;
		}
		curr_elem = curr_elem->next;
	}

	if (check_tid == -1){
		return TID_ERROR;
		// syscall_exit(TID_ERROR);
	}

	sema_down(&child->fork_sema);
	if (child->exit_status == TID_ERROR){
		return TID_ERROR;
		// syscall_exit(TID_ERROR);
	}

	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (!is_user_vaddr(va))
	{
		return true;
	}
	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
	{
		return false;
	}
	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct intr_frame *parent_if = (struct intr_frame *)aux;
	struct thread *current = thread_current();
	struct thread *parent = current->parent;
	struct file *parent_file = NULL;
	bool succ = true;
	/* 1. Read the cpu context to local stack. */
	memcpy(&if_, parent_if, sizeof(struct intr_frame));

	// if_.R.rax = 0; // 추가

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current);
#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif
	// lock_acquire(&fork_lock);
	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	for (int i = 0; i < 64; i++)
	{
		if (parent->fd_table[i] != NULL)
		{
			struct file *new_file = file_duplicate(parent->fd_table[i]);
			if (new_file == NULL)
			{
				// lock_release(&filesys_lock);
				goto error;
			}
			current->fd_table[i] = new_file;
		}
	}
	// lock_release(&fork_lock);
	current->fd = parent->fd;

	sema_up(&current->fork_sema);
	// process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
	{
		if_.R.rax = 0;
		do_iret(&if_);
	}

error:
	sema_up(&current->fork_sema);
	syscall_exit(TID_ERROR);
	// thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */

// f_name : args-single onearg
int process_exec(void *f_name)
{
	char *file_name = f_name;
	char *fn_copy;
	// char *token, *save_ptr;
	bool success;
	fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy(fn_copy, file_name, PGSIZE);

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup();

	/* And then load the binary */
	// success = load(file_name, &_if);
	success = load(fn_copy, &_if);

	/* If load failed, quit. */
	// if (!success){
	// 	palloc_free_page(file_name);
	// 	return -1;
	// }

	/* Start switched process. */
	if (!success)
	{
		palloc_free_page(fn_copy);
		return -1;
	}
	palloc_free_page(fn_copy);
	do_iret(&_if);
	NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	struct thread *curr = thread_current();
	struct thread *child = NULL;
	// struct thread *head = list_entry(list_begin(&curr->children), struct thread, ch_elem);
	struct list_elem *curr_elem = list_begin(&curr->children);
	int check_tid = -1;

	// 자식 tid 일치하는 스레드 찾아서 thread 반환하기.
	while (curr_elem != list_end(&curr->children))
	{
		child = list_entry(curr_elem, struct thread, ch_elem);
		check_tid = child->tid;
		if (check_tid == child_tid)
		{
			break;
		}
		curr_elem = curr_elem->next;
	}

	// 못 찾은 경우
	if (check_tid == -1)
	{
		return -1;
	}

	// 찾으면 넘어간다.
	if (child->wait_check == 1)
	{
		return -1;
	}
	child->wait_check = 1;
	// 자식을 sema_down
	sema_down(&child->wait_sema);
	// 자식 종료
	int status = child->exit_status;
	list_remove(curr_elem);
	sema_up(&child->child_sema);

	return status;
}

/* Exit the process. This function is called by thread_exit (). */
// void process_exit(void)
// {
// 	struct thread *curr = thread_current();
// 	/* TODO: Your code goes here.
// 	 * TODO: Implement process termination message (see
// 	 * TODO: project2/process_termination.html).
// 	 * TODO: We recommend you to implement process resource cleanup here. */

// 	if (curr->running_file != NULL){
// 		// file_allow_write(&curr->running_file);
// 		file_close(curr->running_file);
// 	}
// 	for(int i = 0; i < 64; i++){
// 		if(curr->fd_table[i] != NULL)
// 		{
// 			syscall_close(i);
// 		}
// 	}

// 	sema_up(&curr->wait_sema);
// 	sema_down(&curr->child_sema);

// 	process_cleanup();
// }

void process_exit(void)
{
	struct thread *curr = thread_current();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	if (curr->running_file != NULL){
		// file_allow_write(curr->running_file);
		file_close(curr->running_file);
		curr->running_file = NULL;
	}

	sema_up(&curr->wait_sema);
	struct list_elem *e, *next;
	for (e = list_begin(&curr->children); e != list_end(&curr->children); e = next)
	{
		next = list_next(e);
		struct thread *child = list_entry(e, struct thread, ch_elem);
		if (!child->wait_check)
		{
			sema_up(&child->child_sema); // 자식도 언블럭되도록 함
		}
	}
	sema_down(&curr->child_sema);
	for (int i = 0; i < 64; i++)
	{
		if (curr->fd_table[i] != NULL)
		{
			file_close(curr->fd_table[i]);
			curr->fd_table[i] = NULL;
		}
	}
	
	process_cleanup();
}


/* Free the current process's resources. */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0			/* Ignore. */
#define PT_LOAD 1			/* Loadable segment. */
#define PT_DYNAMIC 2		/* Dynamic linking info. */
#define PT_INTERP 3			/* Name of dynamic loader. */
#define PT_NOTE 4			/* Auxiliary info. */
#define PT_SHLIB 5			/* Reserved. */
#define PT_PHDR 6			/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
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

struct ELF64_PHDR
{
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

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes,
						 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
/* FILE_NAME에서 ELF 실행 파일을 현재 스레드로 로드합니다.
 * 실행 파일의 진입 지점(entry point)은 *RIP에 저장하고,
 * 초기 스택 포인터(stack pointer)는 *RSP에 저장합니다.
 * 로드에 성공하면 true를 반환하고,
 * 실패하면 false를 반환합니다.
 */

static bool
load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/*------------------[Project2 - Argument Passing]------------------*/
	char *token, *save_ptr, *fn_copy;
	char *argv[99];
	int argc = 0;
	fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		return TID_ERROR;

	strlcpy(fn_copy, file_name, PGSIZE);
	for (token = strtok_r(fn_copy, " ", &save_ptr); token != NULL;
		 token = strtok_r(NULL, " ", &save_ptr))
	{
		argv[argc] = token;
		argc++;
	}

	// token = strtok_r(fn_copy, " ", &save_ptr);
	// for( argc = 0; token != NULL;){
	// 	argv[argc] = token;
	// 	argc++;
	// 	token = strtok_r(NULL, " ", &save_ptr);
	// }


	file_name = argv[0];
	char *pos[argc];

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create();
	if (t->pml4 == NULL)
		goto done;
	process_activate(thread_current()); 


	/* Open executable file. */
	file = filesys_open(file_name);
	if (file == NULL)
	{	
		printf("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
		|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	file_deny_write(file);
	thread_current()->running_file = file;

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
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
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment(file, file_page, (void *)mem_page,
								  read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}

	/* Set up stack. */
	if (!setup_stack(if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	// /*------------------[Project2 - Argument Passing]------------------*/
	for (int j = argc - 1; j >= 0; j--)
	{
		if_->rsp = if_->rsp - (strlen(argv[j]) + 1);
		pos[j] = if_->rsp;
		memcpy(if_->rsp, argv[j], strlen(argv[j]) + 1);
	}
	if_->rsp = if_->rsp & ~0x7;
	if_->rsp = if_->rsp - sizeof(char *);
	memset(if_->rsp, 0, sizeof(char *));
	for (int j = argc - 1; j >= 0; j--)
	{
		if_->rsp = if_->rsp - 8;
		memcpy(if_->rsp, &pos[j], 8);
	}
	if_->R.rsi = (uint64_t)if_->rsp;
	if_->R.rdi = argc;
	if_->rsp = if_->rsp - sizeof(void (*)());
	memset(if_->rsp, 0, sizeof(void (*)()));

	/*------------------[Project2 - Argument Passing]------------------*/
// {
//     /* 1) 복사한 문자열의 주소를 저장할 배열 */
//     void *argv_addr[argc];

//     /* 2) 역순으로 문자열 복사 */
//     for (int j = argc - 1; j >= 0; j--) {
//         size_t len = strlen(argv[j]) + 1;
//         if_->rsp -= len;
//         memcpy(if_->rsp, argv[j], len);
//         argv_addr[j] = (void*)if_->rsp;
//     }

//     /* 3) 8바이트 정렬 */
//     uintptr_t align = if_->rsp & 0x7;
//     if_->rsp -= align;
//     memset(if_->rsp, 0, align);

//     /* 4) argv[argc] = NULL */
//     if_->rsp -= sizeof(char*);
//     *(char**)if_->rsp = NULL;

//     /* 5) argv 포인터들 푸시 */
//     for (int j = argc - 1; j >= 0; j--) {
//         if_->rsp -= sizeof(char*);
//         memcpy(if_->rsp, &argv_addr[j], sizeof(char*));
//     }

//     /* 6) argv 배열 주소 푸시 */
//     char **argv_on_stack = (char**)if_->rsp;
//     if_->rsp -= sizeof(char**);
//     memcpy(if_->rsp, &argv_on_stack, sizeof(char**));

//     /* 7) argc 푸시 */
//     if_->rsp -= sizeof(int);
//     *(int*)if_->rsp = argc;

//     /* 8) fake return 주소(NULL) 푸시 */
//     if_->rsp -= sizeof(void*);
//     *(void**)if_->rsp = NULL;

//     /* 9) 레지스터에 argc/argv 설정 */
//     if_->R.rdi = argc;
//     if_->R.rsi = (uint64_t)argv_on_stack;
// }	

	

	success = true;

done:
	/* We arrive here whether the load is successful or not. */

	// file_close(file);
	if (!success && file != NULL)
        file_close(file);
	palloc_free_page(fn_copy);
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
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
static bool install_page(void *upage, void *kpage, bool writable);

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
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
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
/*유저스택 만들어주는 함수*/
static bool
setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
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
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment(struct page *page, void *aux)
{
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
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer(VM_ANON, upage,
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
setup_stack(struct intr_frame *if_)
{
	bool success = false;
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */