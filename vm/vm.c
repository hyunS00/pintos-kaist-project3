/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

/* functions added. */
uint64_t page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED);

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	
	/* init frame_table */
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Returns a hash value for page p. */
uint64_t
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->va < b->va;
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
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
		switch (VM_TYPE(type)) {
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
		if (!spt_insert_page(spt, new_page)) {
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
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* malloc으로 해야 thread_current: is_thread ASSERT 안뜸 */
	struct page p;
	struct hash_elem *e;

	/* TODO: Fill this function. */
	
	/* va가 page의 시작점을 가리키고 있지 않을 수 있으므로
		round_down을 통해 page 시작점의 주소를 구한다. */
	p.va = pg_round_down(va);
	
	/* page->va에 해당하는 주소에 위치한 hash_elem을 찾아온다. */
	e = hash_find (&spt->spt_hash, &p.hash_elem);

	/* 해당하는 hash_elem을 찾을 수 없으면 NULL을 리턴한다. */
	/* 해당하는 page가 존재하므로 page 구조체를 구해서 리턴한다. */
  	return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	bool succ = false;
	/* TODO: Fill this function. */
	
	/* insert에 성공하면 e == NULL이 된다.(hash_insert에 의해) */
	if (!hash_insert(&spt->spt_hash, &page->hash_elem))
		succ = true;

	return succ;
}

/* spt에서 한 page씩 삭제한다. */
void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	/* 여기서 swap out 구현 */
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	/* TODO: Fill this function. */
	struct frame *frame = (struct frame *) malloc(sizeof(struct frame));
	if (frame == NULL)
		return NULL;

	frame->kva = palloc_get_page(PAL_USER);

	/* 할당 가능한 물리 프레임이 없으므로 swap out을 해야 하지만,
		일단 PANIC(todo)로 둔다. */
	if (frame->kva == NULL) {
		/* vm_evict_frame 호출 */
		PANIC("todo");
	}

	// ASSERT (frame != NULL);
	// ASSERT (frame->page == NULL);

	/* frame table에 생성된 frame을 추가해준다. */
	list_push_front(&frame_table, &frame->frame_elem);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;

	/* 1. 유효한 페이지인지 확인한다. */
	if (!user)
		exit(-1);

	/* spt에서 해당하는 page를 찾아온다. */
	addr = pg_round_down(addr);
	page = spt_find_page(spt, addr);

	/* 2. not_present - true: 해당 가상 주소에 대한 페이지가 메모리에 없는 상태 
						false: 해당 가상 주소에 대한 페이지는 메모리에 존재하지만,
								r/o 페이지에 write 시도 */
	if (not_present) {
		if (page == NULL)
			return vm_alloc_page_with_initializer(VM_ANON, addr, write, NULL, NULL);
		else
			return vm_do_claim_page(page);
	}

	/* 3. Bogus fault인지 확인한다. */
	/* 3-1. 이미 초기화된 페이지 (즉 UNINIT이 아닌 페이지)에 PF 발생:
			swap-out된 페이지에 대한 PF이다. */
	if (page->type != VM_UNINIT || page != NULL) {
		if (vm_do_claim_page(page))
			return true;
	}

	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	/* TODO: Fill this function */
	
	struct thread *curr = thread_current();

	va = pg_round_down(va);
	struct page *page = spt_find_page(&curr->spt, va);
	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	if (frame == NULL)
		return false;

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *t = thread_current();
	
	/* page - frame이 매핑되어 있지 않다면 */
	if (pml4_get_page(t->pml4, page->va) == NULL) {
		/* 매핑을 시킨다. */
		if (!pml4_set_page(t->pml4, page->va, frame->kva, page->writable)) {
			free(frame);
			return false;
		}
	}

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	/* 여기서 할당도 같이 해주고 있음 */
	if (!hash_init(&spt->spt_hash, page_hash, page_less, NULL))
		exit(-1);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	
	// struct hash_iterator i;
	// struct hash *h = &src->spt_hash;
	// hash_first(&i, h);
	
	// while (hash_next(&i)) {
	// 	struct page *page = hash_entry(hash_cur(&i), struct page, hash_elem);

	// 	struct page *new_page = (struct page *)malloc(sizeof(struct page));
		
	// 	new_page->operations = page->operations;
	// 	new_page->va = page->va;
	// 	new_page->frame = page->frame;
	// 	new_page->hash_elem = page->hash_elem;
	// 	new_page->type = page->type;
	// 	new_page->writable = page->writable;

	// 	switch (page->type) {
	// 		case VM_ANON:
	// 			break;
	// 		case VM_FILE:
	// 			{
	// 			struct file_page *fp = &page->file;
	// 			new_page->file = page->file;
	// 			break;
	// 			}
	// 		default:
	// 			break;
	// 	}

	// 	bool success = vm_claim_page(new_page);
	// 	if (!success)
	// 		return false;
	// }

	// return true;

	// struct hash_iterator i;
	// hash_first(&i, &src->spt_hash);
	// while (hash_next(&i))
	// {
	// 	struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
	// 	enum vm_type type = src_page->operations->type;
	// 	void *upage = src_page->va;
	// 	bool writable = src_page->writable;
	// 	if (type == VM_UNINIT)
	// 	{
	// 		vm_initializer *init = src_page->uninit.init;
	// 		void *aux = src_page->uninit.aux;
	// 		if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
	// 			return false;
	// 	}
	// 	else
	// 	{
	// 		if (!vm_alloc_page(type, upage, writable))
	// 			return false;
	// 		if (!vm_claim_page(upage))
	// 			return false;
	// 		struct page *dst_page = spt_find_page(dst, upage);
	// 		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	// 	}
	// }
	// return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and -> 이거까지는 구현 완
	 * TODO: writeback all the modified contents to the storage. -> 나중에 구현해야함 */
	
	/* 1. hash_destroy에서 buckets에 달린 page와 vme를 삭제해주어야 한다. */
	/* bucket에 대한 해제는 hash_destroy에서,
		page와 vme에 대한 해제는 hash_free_func에서 수행한다. */
	hash_destroy(&spt->spt_hash, hash_free_func);
}
