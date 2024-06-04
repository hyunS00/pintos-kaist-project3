/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "kernel/hash.h"
#include "include/threads/mmu.h"
#include "include/vm/uninit.h"


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

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check whether the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL) {
		/* Create a new page with uninit_new(). */
		struct page *new_page = (struct page *)malloc(sizeof(struct page));
		if (new_page == NULL) {
			goto err;
		}

		upage = pg_round_down(upage);

		/* Set the operations according to the page type. */
		switch (VM_TYPE(type)) {
			case VM_ANON:
				uninit_new(new_page, upage, init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
				break;
			default:
				goto err;
		}

		/* Insert the page into the spt. */
		if (!spt_insert_page(spt, new_page)) {
			/* Failed to insert the page into spt. Free the allocated page. */
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
	struct page page;
	/* TODO: Fill this function. */
	struct hash_elem *e;
	page.va = pg_round_down(va);

	e = hash_find(&spt->ht, &page.hash_elem);

	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	int succ = false;
	struct hash_elem *e = hash_insert(&spt->ht, &page->hash_elem);

	if (e == NULL)
		succ = true;

	return succ;
}

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

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *) malloc(sizeof (struct frame));
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	if (frame->kva == NULL)
		PANIC("todo");
	
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
// 진짜로 page fault인지 아니면 lazy loading인지 확인한다
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	// 잘못된 사용자 포인터로 인한 폴트 처리
	if (addr == NULL)
		return false;

	/* 유저스택 */
	if (!user)
		return false;

	if(write && !page->writable)
		return false;

	addr = pg_round_down(addr);
	page = spt_find_page(&spt->ht, addr);
	if (page = NULL)
		return false;
	
	if (page -> type!=0) { // page not initialize 타입이 아닐 때 
		return false;
	}
	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
// 
bool
vm_claim_page (void *va UNUSED) {
	va = pg_round_down(va);
	
	struct page *page = spt_find_page(&thread_current()->spt.ht, va);
	/* TODO: Fill this function */
	if (page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
// page <-> frame mapping 이후 swap_in 호출
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *t = thread_current();
	if (pml4_get_page (t->pml4, page->va != NULL))
		return false;
	
	pml4_set_page (t->pml4, page->va, frame->kva, page->writable);

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->ht, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy(&spt->ht, hash_free_func);
}
