#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

/* 처리된 페이지 폴트의 수. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 사용자 프로그램에 의해 발생할 수 있는 인터럽트에 대한 핸들러를 등록한다.

실제 유닉스 계열 운영체제에서는 이러한 인터럽트 대부분이
[SV-386] 3-24 및 3-25에 설명된 바와 같이 시그널 형태로 사용자 프로세스에 전달된다.
하지만 우리는 시그널을 구현하지 않았기 때문에, 단순히 사용자 프로세스를 종료시킨다.

페이지 폴트는 예외다. 여기서는 다른 예외들과 동일하게 처리하지만,
가상 메모리를 구현하려면 이 방식은 변경되어야 한다.

각 예외에 대한 설명은 [IA32-v3a] 섹션 5.15 "Exception and Interrupt Reference"를 참조하라. */
void
exception_init (void) {
	/* 이러한 예외는 INT, INT3, INTO, BOUND 명령어 등을 통해
	사용자 프로그램에 의해 명시적으로 발생시킬 수 있다.
	따라서 DPL을 3으로 설정하여, 사용자 프로그램이 이러한 명령어를 통해
	예외를 호출할 수 있도록 허용한다. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* These exceptions have DPL==0, preventing user processes from
	   invoking them via the INT instruction.  They can still be
	   caused indirectly, e.g. #DE can be caused by dividing by
	   0.  */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill,
			"#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill,
			"#XF SIMD Floating-Point Exception");

	/* Most exceptions can be handled with interrupts turned on.
	   We need to disable interrupts for page faults because the
	   fault address is stored in CR2 and needs to be preserved. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) {
	/* This interrupt is one (probably) caused by a user process.
	   For example, the process might have tried to access unmapped
	   virtual memory (a page fault).  For now, we simply kill the
	   user process.  Later, we'll want to handle page faults in
	   the kernel.  Real Unix-like operating systems pass most
	   exceptions back to the process via signals, but we don't
	   implement them. */

	/* The interrupt frame's code segment value tells us where the
	   exception originated. */
	switch (f->cs) {
		case SEL_UCSEG:
			/* User's code segment, so it's a user exception, as we
			   expected.  Kill the user process.  */
			printf ("%s: dying due to interrupt %#04llx (%s).\n",
					thread_name (), f->vec_no, intr_name (f->vec_no));
			intr_dump_frame (f);
			thread_exit ();

		case SEL_KCSEG:
			/* Kernel's code segment, which indicates a kernel bug.
			   Kernel code shouldn't throw exceptions.  (Page faults
			   may cause kernel exceptions--but they shouldn't arrive
			   here.)  Panic the kernel to make the point.  */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* Some other code segment?  Shouldn't happen.  Panic the
			   kernel. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void		//일부 프로젝트 2에서 page_fault() 함수를 수정해야 할 수도 있다.
page_fault (struct intr_frame *f) {
	bool not_present;  /* True: not-present page, false: writing r/o page. */
	bool write;        /* True: access was write, false: access was read. */
	bool user;         /* True: access by user, false: access by kernel. */
	void *fault_addr;  /* Fault address. */

	/* Obtain faulting address, the virtual address that was
	   accessed to cause the fault.  It may point to code or to
	   data.  It is not necessarily the address of the instruction
	   that caused the fault (that's f->rip). */

	fault_addr = (void *) rcr2();

	/* Turn interrupts back on (they were only off so that we could
	   be assured of reading CR2 before it changed). */
	intr_enable ();


	/* Determine cause. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* For project 3 and later. */
	if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
#endif

	/* Count page faults. */
	page_fault_cnt++;

	/* If the fault is true fault, show info and exit. */
	printf ("Page fault at %p: %s error %s page in %s context.\n",
			fault_addr,
			not_present ? "not present" : "rights violation",
			write ? "writing" : "reading",
			user ? "user" : "kernel");
	kill (f);
}

