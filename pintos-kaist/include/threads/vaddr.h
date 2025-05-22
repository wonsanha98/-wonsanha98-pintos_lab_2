#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* Functions and macros for working with virtual addresses.
 *
 * See pte.h for functions and macros specifically for x86
 * hardware page tables. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* Page offset (bits 0:12). */
#define PGSHIFT 0                          /*가상 주소에서 오프셋(Offset)비트가 시작되는 비트 인덱스를 나타낸다.*/
#define PGBITS  12                         /* 페이지 크기를 표현하는 비트 수 12비트, 4KB*/
#define PGSIZE  (1 << PGBITS)              /* 페이지의 크기를 바이트 단위로 나타낸 값, 4095바이트(4KB) */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* 페이지 오프셋 부분, 가상 주소에서 페이지 오프셋만 추출할 때 사용 */

/* Offset within a page. */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK)	//가상 주소 va에서 페이지 오프셋(하위 12비트)만 추출하여 변환하는 매크로

#define pg_no(va) ((uint64_t) (va) >> PGBITS) 	//가상 주소 va에서 페이지 번호를 추출해서 반환한다, 페이지 오프셋을 제외한 상위 비트 반환

/* Round up to nearest page boundary. */
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK)) //가상 주소 va가 포함된 페이지의 시작 주소 반환, va에서 페이지 오프셋을 0으로 만든 주소

/* Round down to nearest page boundary. */
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK)

/* Kernel virtual address start */
#define KERN_BASE LOADER_KERN_BASE	//커널 가상 메모리의 시작 주소로, 기본값은 0x8004000000이다.

/* User stack start */
#define USER_STACK 0x47480000

/* VADDR가 사용자 가상 주소인 경우 true를 반환합니다. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr))) //주어진 주소 vaddr가 사용자 가상 메모리 범위에 속하면 true를 반환, vaddr < KERN_BASE

/* "VADDR가 커널 가상 주소이면 true를 반환합니다 */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)	//vaddr가 커널 가상 메모리 범위에 속하면 true를 반환, vaddr >= KERN_BASE

// FIXME: add checking
/* Returns kernel virtual address at which physical address PADDR
 *  is mapped. */ //paddr은 반드시 0 이상이고, 시스템의 실제 물리 메모리 크기 이하여야 한다.(시스템의 실제 물리 메모리 크기는?)
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE))	//주어진 물리 주소 paddr에 대응되는 커널 가상 주소를 반환한다.

/* Returns physical address at which kernel virtual address VADDR
 * is mapped. */	//주어진 커널 가상 주소 vaddr 대응되는 물리 주소를 반환한다. vaddr은 반드시 KERN_BASE 이상의 주소여야 하며, 커널 가상 메모리 영역에 속해야 한다.
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
})

#endif /* threads/vaddr.h */
