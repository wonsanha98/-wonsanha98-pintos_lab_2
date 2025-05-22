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

/* 페이지 폴트 핸들러.
이 코드는 가상 메모리를 구현하기 위해 작성되어야 하는 뼈대(skeleton)입니다.
프로젝트 2에 대한 일부 해결책은 이 코드를 수정해야 할 수도 있습니다.

진입 시, 오류가 발생한 주소는 CR2(제어 레지스터 2)에 저장되어 있으며,
오류에 대한 정보는 exception.h에 정의된 PF_* 매크로 형식으로 F의 error_code 멤버에 포함되어 있습니다.
아래 예제 코드는 해당 정보를 어떻게 해석하는지 보여줍니다.
이들에 대한 더 자세한 정보는 [IA32-v3a] 문서의 5.15절 "예외 및 인터럽트 참조
(Interrupt 14--Page Fault Exception (#PF))"에서 확인할 수 있습니다.*/
static void		//일부 프로젝트 2에서 page_fault() 함수를 수정해야 할 수도 있다.
page_fault (struct intr_frame *f) {
	bool not_present;  /* True: not-present page, false: writing r/o page. */
	bool write;        /* True: access was write, false: access was read. */
	bool user;         /* True: access by user, false: access by kernel. */
	void *fault_addr;  /* Fault address. */

	/* 오류(fault)를 발생시킨 주소, 즉 접근으로 인해 오류가 발생한 가상 주소를 가져옵니다.
	   이 주소는 코드나 데이터 중 어느 쪽을 가리킬 수도 있습니다.
	   반드시 오류를 일으킨 명령어의 주소(f->rip)와 같지는 않습니다. */

	fault_addr = (void *) rcr2();

	/* 인터럽트를 다시 활성화합니다 
	(CR2 레지스터를 변경되기 전에 안전하게 읽기 위해 잠시 인터럽트를 비활성화했던 것입니다). */
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

