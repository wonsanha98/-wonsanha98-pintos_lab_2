#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* struct thread의 `magic` 멤버를 위한 임의의 값.
   스택 오버플로우를 감지하는 데 사용됨. 자세한 내용은 thread.h 상단의 큰 주석 참고. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드를 위한 임의의 값.
   이 값은 수정하지 마세요. */
#define THREAD_BASIC 0xd42df210

/* THREAD_READY 상태의 프로세스 리스트. 즉, 실행 준비는 되었지만 아직 실행되지 않은 프로세스들. */
static struct list ready_list;

/*thread_sleep()을 통해 block된 thread들. wakeup을 통해 ready_list로 이동*/
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* 초기 스레드. 즉, init.c의 main()을 실행하는 스레드. */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 락. */
static struct lock tid_lock;

/* 삭제 요청된 스레드 목록 */
static struct list destruction_req;

/* 통계 정보. */
static long long idle_ticks;   /* idle 상태로 소비된 timer tick 수 */
static long long kernel_ticks; /* 커널 스레드에서 소비된 timer tick 수 */
static long long user_ticks;   /* 사용자 프로그램에서 소비된 timer tick 수 */

/* 스케줄링 */
#define TIME_SLICE 4		  /* 각 스레드에 할당할 timer tick 수 */
static unsigned thread_ticks; /* 마지막 yield 이후 경과된 timer tick 수 */

/* false가 기본값이면 round-robin 스케줄러 사용.
   true면 multi-level feedback queue 스케줄러 사용.
   이는 커널 명령줄 옵션 "-o mlfqs"로 제어됩니다. */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
// 현재 구조체가 쓰레드인지 확인
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
// cpu의 현재 실행중인 쓰레드 반환, 
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
// 시작시 쓰레드 사용을 위해 초기화하는 함수.
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);			// cpu 자원 쓰는 것을 선점하고 관리하기 위한 lock
	list_init (&ready_list);
	list_init (&destruction_req);	//쓰레드 폐기 요청 리스트
	list_init (&sleep_list);
	global_tick = INT64_MAX; // global tick init - ch

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
   // 인터럽트 활성화하고 유후 쓰레드를 생성해서 cpu 선점해놓는 함수
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);	// lock도 걸어놓기
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
// 통계를 표시하기 위한 tick 계산 함수, 각 타이머 틱마다 timer_interrupt를 통해 호출된다.
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
// 통계 표시 함수
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
// 새로운 커널 쓰레드 생성 함수. thread_func를 실행하며 aux에 넣은 인자를 thread_func에 전달하면 해당 함수를 쓰레드가 실행한다.
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	struct thread *curr = thread_current();
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	
	for(int i = 0; i < 64; i++)
	{
		t->fd_table[i] == NULL;
	}
	t->fd = -1;


	t->parent = curr;
	//부모 children list 에 insert 한다.
	list_push_back(&curr->children, &t->ch_elem);
	/* Add to run queue. */
	//ready_list 에 넣어준다.
	thread_unblock (t);
	

	/* compare the priorities of the currently running
	thread and the newly inserted one. Yield the CPU if the
	newly arriving thread has higher priority*/
	if(thread_current()->priority < priority){
		thread_yield();
	}
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
// 현재 실행중인 쓰레드의 상태를 블록으로 변경하고 schedule()을 호출해 다음 쓰레드 실행
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
// 인터럽트를 비활성화하고 블록되어있는 쓰레드를 ready_list에 추가한다.
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered(&ready_list, &t->elem, priority_more, NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
// 현재 실행되고있는 쓰레드를 반환한다.
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
// 현재 쓰레드 tid반환
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
// 현재 실행중인 쓰레드의 상태를 폐기로 바꾸고 다음 스레드 실행시킨다.
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
// 현재실행중인 쓰레드를 준비상태로 ready_list의 마지막에 넣는다.
void
thread_yield (void) {
	struct thread *curr = thread_current ();
		if (curr == idle_thread){
		return;
	}
	enum intr_level old_level;
	
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, priority_more, NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

// 현재 쓰레드를 블록하고  sleep_list로 이동시킨다.
// list_insert_ordered()를 통해 sleep_list 내부를 getuptick순으로 정렬한다.
void thread_sleep(int64_t getuptick)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();
	if (curr != idle_thread) // 현재 쓰레드가 idle_thread가 아니라면
	{
		curr->getuptick = getuptick; // 깨어날 시간을 getupick으로 저장
		list_insert_ordered(&sleep_list, &curr->elem, getuptick_less, NULL);
		struct thread *target = list_entry(list_begin(&sleep_list), struct thread, elem); // 리스트에 정렬 삽입
		global_tick = target->getuptick;
		thread_block();
	}
	intr_set_level(old_level); // 인터럽트 수준을 원래 상태로 설정한다.
}

// sleep_list에서 가장 현재 global_tick과 깨어날 시간이 같은 쓰레드를 ready_list로 이동시킨다.
void wakeup(void)
{
	if (list_empty(&sleep_list)){
		return;
	}

	enum intr_level old_level;

	old_level = intr_disable(); // 인터럽트 비활성화

	struct list_elem *start = list_begin(&sleep_list);

	while (start != list_end(&sleep_list))
	{
		struct thread *start_thread = list_entry(start, struct thread, elem);
		if (start_thread->getuptick <= global_tick)
		{
			struct list_elem *next = start->next;
			list_pop_front(&sleep_list);
			thread_unblock(start_thread);
			start = next;
		}
		else
		{
			break;
		}
	}

	if (!list_empty(&sleep_list))
	{
		global_tick = list_entry(start, struct thread, elem)->getuptick; // 현재 쓰레드에 저장된 값을 변경
	}
	else
	{
		global_tick = INT64_MAX;
	}

	intr_set_level(old_level); // 인터럽트 수준을 원래 상태로 설정한다.
}

// getuptick순으로 정렬하는 내부함수
bool getuptick_less(const struct list_elem *a_, const struct list_elem *b_,
					void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->getuptick < b->getuptick;
}

// priority compare helper function
bool priority_more(const struct list_elem *a_, const struct list_elem *b_,
					void *aux UNUSED)
{
	const struct thread *a = list_entry(a_, struct thread, elem);
	const struct thread *b = list_entry(b_, struct thread, elem);

	return a->priority > b->priority;
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	thread_current()->origin_priority = new_priority;
	refresh_priority();

	if (thread_get_priority() < list_entry(list_begin(&ready_list), struct thread, elem)->priority)
	{
		thread_yield();	
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);		// 유후 쓰레드이기 때문에 세마포어 한칸 늘려놓기

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
// 함수를 커널 스레드로 생성하는 래퍼함수
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
// 새로운 쓰레드를 생성하는 단계에서 값을 초기화하는 함수. memset으로 메모리 영역을 초기화하고, 초기화할 쓰레드의 값을 초기값으로 설정한다.
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->origin_priority = priority;
	t->magic = THREAD_MAGIC;
	/*------------------[Project1 - Thread]------------------*/
	t->getuptick = 0;
	list_init(&t->donations);
	list_init(&t->children);
	t->wait_on_lock = NULL;
	sema_init(&t->wait_sema,0);
	sema_init(&t->child_sema,0);
	sema_init(&t->fork_sema, 0);

}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
// ready_list가 비어있다면 idle_thread를 반환하고, 비어있지 않다면 다음 값을 pop해서 반환한다.
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
// iret는 interrupt return이란 뜻으로 쓰레드를 컨텍스트 전환에 필요한 어셈블리어 명령어들을 실행하는 함수입니다.
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
// 쓰레드의 컨텍스트를 전환하는 함수. 전체 실행 컨텍스트를 intr_frame에 저장한다. 
// 즉, 현재 실행중이었던 쓰레드의 정보를 백업해놓는 함수. 그리고 do_iret함수를 호출하는 것을 통해 다음 쓰레드로 컨텍스트를 바꾼다.
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
// 소멸시켜야할 쓰레드들을 정리하고 현재 쓰레드를 매개변수로 받은 상태로 바꾸며 다음 쓰레드를 실행하는 schedule()을 호출한다.
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

// ready_list의 다음 쓰레드를 실행하는 함수
static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
// lock을 활용하여 다음 tid값을 할당해주는 함수. 락이 대기상태에서 사용가능한 상태로 변경될 때 값을 하나 올려서 tid를 할당한다.
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}