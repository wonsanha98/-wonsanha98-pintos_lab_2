#ifndef THREAD_MMU_H
#define THREAD_MMU_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/pte.h"

//사용자 정의 함수 포인터 타입으로, 각 PTE에 대해 수행할 함수의 형태를 정의한다.
//pte : 현재 페이지 테이블 항목
//va : 해당 항목에 대응되는 가상 주소
//aux : 부가적인 데이터(사용자 정의 목적)
typedef bool pte_for_each_func (uint64_t *pte, void *va, void *aux);

uint64_t *pml4e_walk (uint64_t *pml4, const uint64_t va, int create);
uint64_t *pml4_create (void);
//주어진 pml4 루트 페이지 테이블에 대해, 유효한 항목마다 func(pte, va, aux)를 호출한다.
//만약 func가 false를 반환하면 순회를 멈추고 전체 함수도 false를 반환한다.
//va는 해당 항목이 가리키는 가상 주소이다.
bool pml4_for_each (uint64_t *, pte_for_each_func *, void *); 
void pml4_destroy (uint64_t *pml4);
//커널이 한 프로세스에서 다른 프로세스로 전환할 때, 
//프로세서의 페이지 디렉터리 기준 레지스터를 변경하여 사용자 가상 주소 공간도 전환된다.
void pml4_activate (uint64_t *pml4); 
void *pml4_get_page (uint64_t *pml4, const void *upage);
bool pml4_set_page (uint64_t *pml4, void *upage, void *kpage, bool rw);
void pml4_clear_page (uint64_t *pml4, void *upage);
bool pml4_is_dirty (uint64_t *pml4, const void *upage);
void pml4_set_dirty (uint64_t *pml4, const void *upage, bool dirty);
bool pml4_is_accessed (uint64_t *pml4, const void *upage);
void pml4_set_accessed (uint64_t *pml4, const void *upage, bool accessed);

#define is_writable(pte) (*(pte) & PTE_W) //해당 페이지 테이블 항목이 가리키는 가상 주소 공간이 쓰기 가능한지 여부를 반환한다.
#define is_user_pte(pte) (*(pte) & PTE_U) //주어진 페이지 테이블 항목 pte가 사용자 영역(User)에 속한 항목인지 여부를 판별한다.
#define is_kern_pte(pte) (!is_user_pte (pte)) //pte가 커널(Kernel) 영역에 속한지 여부를 판별한다

#define pte_get_paddr(pte) (pg_round_down(*(pte)))

/* Segment descriptors for x86-64. */
struct desc_ptr {
	uint16_t size;
	uint64_t address;
} __attribute__((packed));

#endif /* thread/mm.h */
