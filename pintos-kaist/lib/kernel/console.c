#include <console.h>
#include <stdarg.h>
#include <stdio.h>
#include "devices/serial.h"
#include "devices/vga.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

static void vprintf_helper (char, void *);
static void putchar_have_lock (uint8_t c);

/* 콘솔 락.
VGA와 시리얼 계층은 각각 자체적인 락을 수행하므로, 언제든지 호출해도 안전하다.
하지만 이 락은 동시에 여러 printf() 호출이 출력 내용을 섞어 
혼란스럽게 만드는 것을 방지하는 데 유용하다. */
static struct lock console_lock;

/* 일반적인 상황에서는 true이다. 위에서 설명한 것처럼 스레드 간 출력이 섞이지 않도록 콘솔 락을 사용하고자 하기 때문이다.

하지만 시스템이 부팅 초기에 아직 락이 작동하지 않거나 콘솔 락이 초기화되지 않은 시점, 혹은 커널 패닉 이후에는 false이다.
초기 부팅 단계에서 락을 획득하려 하면 어설션 실패로 이어지고, 이는 다시 패닉을 유발하게 되어 후자의 상황으로 이어진다.
또한 커널 패닉 상황에서 그 원인이 lock_acquire() 구현의 버그일 경우, 재귀적으로 계속 호출될 수 있다. */
static bool use_console_lock;

/* Pintos에 디버그 출력을 충분히 추가하면, 하나의 스레드에서 console_lock을 재귀적으로 획득하려고 시도하는 상황이 발생할 수 있다.
실제 예로, 내가 palloc_free()에 printf() 호출을 추가했을 때 다음과 같은 실제 백트레이스가 발생했다.:

   lock_console()
   vprintf()
   printf()             - palloc() tries to grab the lock again
   palloc_free()        
   schedule_tail()      - another thread dying as we switch threads
   schedule()
   thread_yield()
   intr_handler()       - timer interrupt
   intr_set_level()
   serial_putc()
   putchar_have_lock()
   putbuf()
   sys_write()          - one process writing to the console
   syscall_handler()
   intr_handler()

   이러한 문제는 디버깅하기 매우 어렵기 때문에, 
   우리는 깊이 카운터를 사용하여 재귀 락을 시뮬레이션함으로써 문제를 피한다. */
static int console_lock_depth;

/* 콘솔에 출력된 문자 수. */
static int64_t write_cnt;

/* Enable console locking. */
void
console_init (void) {
	lock_init (&console_lock);
	use_console_lock = true;
}

/* Notifies the console that a kernel panic is underway,
   which warns it to avoid trying to take the console lock from
   now on. */
void
console_panic (void) {
	use_console_lock = false;
}

/* Prints console statistics. */
void
console_print_stats (void) {
	printf ("Console: %lld characters output\n", write_cnt);
}

/* 콘솔 락을 획득한다. */
	static void
acquire_console (void) {
	if (!intr_context () && use_console_lock) {
		if (lock_held_by_current_thread (&console_lock)) 
			console_lock_depth++; 
		else
			lock_acquire (&console_lock); 
	}
}

/* Releases the console lock. */
static void
release_console (void) {
	if (!intr_context () && use_console_lock) {
		if (console_lock_depth > 0)
			console_lock_depth--;
		else
			lock_release (&console_lock); 
	}
}

/* Returns true if the current thread has the console lock,
   false otherwise. */
static bool
console_locked_by_current_thread (void) {
	return (intr_context ()
			|| !use_console_lock
			|| lock_held_by_current_thread (&console_lock));
}

/*vprintf() 표준 함수는 printf()와 유사하지만 va_list를 사용한다.
출력 결과를 VGA 디스플레이와 시리얼 포트에 모두 출력한다. */
int
vprintf (const char *format, va_list args) {
	int char_cnt = 0;

	acquire_console ();
	__vprintf (format, args, vprintf_helper, &char_cnt);
	release_console ();

	return char_cnt;
}

/* 문자열 S를 콘솔에 출력하고, 그 뒤에 줄 바꿈 문자를 출력한다. */
int
puts (const char *s) {
	acquire_console ();
	while (*s != '\0')
		putchar_have_lock (*s++);
	putchar_have_lock ('\n');
	release_console ();

	return 0;
}

/* 버퍼(BUFFER)에 있는 N개의 문자를 콘솔에 출력한다. */
void
putbuf (const char *buffer, size_t n) {
	acquire_console ();
	while (n-- > 0)
		putchar_have_lock (*buffer++);
	release_console ();
}

/* 문자 C를 VGA 디스플레이와 시리얼 포트에 출력한다. */
int
putchar (int c) {
	acquire_console ();
	putchar_have_lock (c);
	release_console ();

	return c;
}

/* Helper function for vprintf(). */
static void
vprintf_helper (char c, void *char_cnt_) {
	int *char_cnt = char_cnt_;
	(*char_cnt)++;
	putchar_have_lock (c);
}

/* 문자 C를 VGA 디스플레이와 시리얼 포트에 출력한다.
필요한 경우 호출자는 이미 콘솔 락을 획득한 상태여야 한다. */
static void
putchar_have_lock (uint8_t c) {
	ASSERT (console_locked_by_current_thread ());
	write_cnt++;
	serial_putc (c);
	vga_putc (c);
}
