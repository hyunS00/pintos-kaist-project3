/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "lib/string.h"
#include "include/vm/file.h"

/* functions added. */
uint64_t page_hash(const struct hash_elem *p_, void *aux UNUSED);
bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED);

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */

	/* init frame_table */
	list_init(&frame_table);
	lock_init(&vm_lock);

	clock_hand = NULL;
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Returns a hash value for page p. */
uint64_t
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* malloc으로 유저 영역에 페이지 하나 할당 */
		/* malloc을 쓰는 이유?
			palloc은 물리 프레임, 즉 vm_get_frame에서 페이지를 생성할 때 사용한다.
			또한, 구현의 편의성을 위해 malloc을 사용한다. */
		struct page *new_page = (struct page *)malloc(sizeof(struct page));
		if (new_page == NULL)
			goto err;

		/* page round down */

		upage = pg_round_down(upage);

		/* type에 맞게 uninit page를 생성한다. */
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			uninit_new(new_page, upage, init, type, aux, anon_initializer);
			break;

		case VM_FILE:
			uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
			break;

		default:
			free(new_page);
			goto err;
		}

		/* 쓰기 권한 업데이트 */
		new_page->writable = writable;

		/* TODO: Insert the page into the spt. */
		if (!spt_insert_page(spt, new_page))
		{
			free(new_page);
			goto err;
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	/* malloc으로 해야 thread_current: is_thread ASSERT 안뜸 */
	struct page p;
	struct hash_elem *e;

	/* TODO: Fill this function. */

	/* va가 page의 시작점을 가리키고 있지 않을 수 있으므로
		round_down을 통해 page 시작점의 주소를 구한다. */
	p.va = pg_round_down(va);

	/* page->va에 해당하는 주소에 위치한 hash_elem을 찾아온다. */
	e = hash_find(&spt->spt_hash, &p.hash_elem);

	/* 해당하는 hash_elem을 찾을 수 없으면 NULL을 리턴한다. */
	/* 해당하는 page가 존재하므로 page 구조체를 구해서 리턴한다. */
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	bool succ = false;
	/* TODO: Fill this function. */

	/* insert에 성공하면 e == NULL이 된다.(hash_insert에 의해) */
	if (!hash_insert(&spt->spt_hash, &page->hash_elem))
		succ = true;

	return succ;
}

/* spt에서 한 page씩 삭제한다. */
void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	struct list_elem *fe;
	struct frame *f;

	lock_acquire(&vm_lock);

	if (clock_hand == NULL)
	{
		clock_hand = list_begin(&frame_table);
	}

	/* 첫번째 for문 : lru clock부터 리스트 끝까지 돌기*/
	for (fe = clock_hand; fe != list_tail(&frame_table); fe = list_next(fe))
	{
		f = list_entry(fe, struct frame, frame_elem);
		if (!pml4_is_accessed(thread_current()->pml4, f->page->va))
		{
			victim = f;
			clock_hand = list_remove(fe);
			if (clock_hand == list_tail(&frame_table))
			{
				clock_hand = list_begin(&clock_hand);
			}
			goto done;
		}
		else
		{
			pml4_set_accessed(thread_current()->pml4, f->page->va, 0);
		}
	}

	/* 두번째 for 문 : 처음부터 lru clock까지 돌기 */
	for (fe = list_begin(&frame_table); fe != list_next(clock_hand); fe = list_next(fe))
	{
		f = list_entry(fe, struct frame, frame_elem);
		if (!pml4_is_accessed(thread_current()->pml4, f->page->va))
		{
			victim = f;
			clock_hand = list_remove(fe);
			if (clock_hand == list_tail(&frame_table))
			{

				clock_hand = list_begin(&clock_hand);
			}
			goto done;
		}
		else
		{
			pml4_set_accessed(thread_current()->pml4, f->page->va, 0);
		}
	}

	goto done;

done:
	lock_release(&vm_lock);
	return victim;
}

static struct frame *
vm_evict_frame(void)
{
	struct frame *victim = NULL;
	while (victim == NULL)
	{
		/* 희생자 선택*/
		victim = vm_get_victim();
		if (victim == NULL)
			return NULL;
		/* 희생자 swap out 처리*/
		if (swap_out(victim->page) == false)
		{
			/* 선택한 희생자 swap out 못 할 시 다른 희생자 선택*/
			victim = NULL;
		}
	}
	return victim;
}

static struct frame *
vm_get_frame(void)
{
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	lock_acquire(&vm_lock);
	frame->kva = palloc_get_page(PAL_USER);
	frame->page = NULL;
	lock_release(&vm_lock);

	if (frame->kva == NULL)
	{
		/* vm_evict_frame 호출 */
		frame = vm_evict_frame();
		frame->page = NULL;
	}
	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	/* frame table에 생성된 frame을 추가해준다. */
	lock_acquire(&vm_lock);
	list_push_front(&frame_table, &frame->frame_elem);
	lock_release(&vm_lock);
	return frame;
}

static void
vm_stack_growth(void *addr)
{
	void *stack_bottom = pg_round_down(addr);
	size_t stack_size = USER_STACK - (uintptr_t)stack_bottom;
	size_t num_pages = (stack_size < PGSIZE) ? 1 : ((stack_size % PGSIZE == 0) ? stack_size / PGSIZE : stack_size / PGSIZE + 1);

	for (size_t i = 0; i < num_pages; i++)
	{
		void *page_addr = (void *)((uintptr_t)stack_bottom + i * PGSIZE);

		if (vm_alloc_page(VM_ANON | VM_MARKER_0, page_addr, true) && !vm_claim_page(page_addr))
		{
			struct page *page = spt_find_page(&thread_current()->spt, page_addr);
			vm_dealloc_page(page);
		}
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{

	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;

	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	/* 1. 커널 주소에대한 접근인지 확인한다 */
	if (is_kernel_vaddr(addr))
		exit(-1);

	uintptr_t rsp;
	if (user)
	{
		rsp = f->rsp;
	}
	else
		rsp = thread_current()->user_rsp;

	/* 스택 성장 가능한지 체크 */
	if (USER_STACK - (1 << 20) <= addr && addr <= USER_STACK && rsp - 8 <= addr)
	{
		vm_stack_growth(addr);
		return true;
	}
	/* spt에서 해당하는 page를 찾아온다. */
	addr = pg_round_down(addr);

	struct page *page = spt_find_page(spt, addr);
	/* 해당 가상 주소에 대한 페이지가 메모리에 없는 상태*/
	if (page == NULL)
		exit(-1);

	if (write && !page->writable)
		exit(-1);

	if (not_present)
	{
		/* 해당 가상 주소에 대한 페이지는 메모리에 존재하지만, r/o 페이지에 write 시도*/
		return vm_do_claim_page(page);
	}

	/* 2. Bogus fault인지 확인한다. */
	/* 2-1. 이미 초기화된 페이지 (즉 UNINIT이 아닌 페이지)에 PF 발생:
			swap-out된 페이지에 대한 PF이다. */
	if (page->operations->type != VM_UNINIT || page != NULL)
	{
		if (vm_do_claim_page(page))
			return true;
	}
	printf("handle fault\n");
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
/* 가상 메모리가 있는지 확인하고 vm_do_cliam_page를 호출하여 물리 프레임 할당을 성공하면 true 반환*/
bool vm_claim_page(void *va UNUSED)
{
	/* TODO: Fill this function */

	struct thread *curr = thread_current();

	va = pg_round_down(va);
	struct page *page = spt_find_page(&curr->spt, va);
	if (page == NULL)
	{
		return false;
	}

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
/* 물리 프레임 할당 및 페이지 테이블에 저장*/
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();
	if (frame == NULL)
		return false;

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *t = thread_current();
	void *pml4 = t->pml4;
	/* page - frame이 매핑되어 있지 않다면 */
	if (pml4_get_page(pml4, page->va) == NULL)
	{
		/* 물리 프레임과 가상 페이지 매핑 */
		if (!pml4_set_page(pml4, page->va, frame->kva, page->writable))
		{
			/* 매핑 되지 않았다면 할당 해제 */
			free(frame);
			return false;
		}
	}
	/* 무조건 물리 메모리와 매핑이 되어 있는 경우이다*/
	/* 따라서 swap_in을 호출할 때 page는 스왑 디스크에 있을 경우가 없다*/
	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	if (!hash_init(&spt->spt_hash, page_hash, page_less, NULL))
		exit(-1);
}

/*
	supplemental_page_table_copy: src에서 dst로 SPT을 복사하는 함수

	자식이 부모의 실행 context를 상속해야 할 때 사용됨 ex) fork()
	src의 SPT에 있는 각 페이지를 반복하여 dst의 SPT에 있는 엔트리의 복사본을 만듦
	uninit 페이지를 할당하고 즉시 claim해야 함
*/
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);
	while (hash_next(&i))
	{
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);

		/* struct page의 field */
		enum vm_type type = page_get_type(src_page);
		void *upage = src_page->va;
		bool writable = src_page->writable;
		vm_initializer *init = src_page->uninit.init;
		void *aux = src_page->uninit.aux;

		switch (src_page->operations->type)
		{
		/* 부모 페이지의 필드들을 갖고와 같은 설정으로 자식 페이지를 할당한다. */
		/* 사실상 UNINIT 페이지는 존재하지 않는다? */
		case VM_UNINIT:
		{
			/* aux가 NULL이 아니라면 페이지 초기화 할 때 aux를 그대로 복사해주면 안 된다.
			자식이 해당 file을 닫아버리면 부모가 사용하지 못 한다*/
			if (aux != NULL)
			{
				struct file_page *new_aux = (struct file_page *)malloc(sizeof(struct file_page));
				memcpy(new_aux, aux, sizeof(struct file_page));
				aux = new_aux;
			}

			if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
				return false;

			break;
		}

		case VM_FILE:
		{
			struct page *dst_page;

			struct file_page *file_page = malloc(sizeof(struct file_page));
			struct file_page *src_file = &src_page->file;
			file_page->file = file_reopen(src_file->file);
			file_page->offset = src_file->offset;
			file_page->read_bytes = src_file->read_bytes;
			file_page->total_page = src_file->total_page;
			file_page->zero_bytes = src_file->zero_bytes;

			/* 페이지 초기화 작업*/
			if (vm_alloc_page_with_initializer(VM_FILE, upage, src_page->writable, NULL, file_page) == false)
			{
				return false;
			}

			/* 할당된 가상 페이지 가져오기*/
			dst_page = spt_find_page(&thread_current()->spt, upage);

			/* 물리 프레임에 할당 작업*/
			if (vm_claim_page(upage) == false)
			{
				vm_dealloc_page(dst_page);
			}

			/* 새롭게 할당된 물리 메모리에 기존에 있던 물리 메모리 내용을 복사한다*/
			memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
			break;
		}

		/* UNINIT 상태가 아니라면 물리 프레임에 매핑하는 작업까지 수행한다. */
		/* aux가 필요하지 않은 이유?
			- aux는 lazy loading을 위해 */
		/* TODO: VM_FILE, VM_ANON에 대해 구분이 필요하다면 추가해주어야 한다. */
		default:
		{
			if (!vm_alloc_page(type, upage, writable) || !vm_claim_page(upage))
			{
				return false;
			}

			struct page *dst_page = spt_find_page(dst, src_page->va);
			if (dst_page == NULL)
			{
				return false;
			}

			memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
		}
		}
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and -> 이거까지는 구현 완
	 * TODO: writeback all the modified contents to the storage. -> 나중에 구현해야함 */
	/* 1. hash_destroy에서 buckets에 달린 page와 vme를 삭제해주어야 한다. */
	/* bucket에 대한 해제는 hash_destroy에서,
		page와 vme에 대한 해제는 hash_free_func에서 수행한다. */
	hash_clear(&spt->spt_hash, hash_free_func);
}
