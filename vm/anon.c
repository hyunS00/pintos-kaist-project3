/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/palloc.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct thread *curr = thread_current();

	/* spt table에서 destroy할 페이지를 삭제한다. */
	// spt_remove_page(&curr->spt, page);

	lock_acquire(&vm_lock);
	if (pml4_get_page(curr->pml4, page->va) != NULL) {
		/* frame_table에서 페이지에 매핑된 프레임을 삭제한다. */
		list_remove(&page->frame->frame_elem);

		/* 페이지 - 프레임 사이 매핑을 해제한다. */
		pml4_clear_page(curr->pml4, page->va);
		palloc_free_page(page->frame->kva);
	}
	lock_release(&vm_lock);


	/* 페이지에 매핑된 프레임이 할당되어 있다면 해제한다. */
	// if (page->frame->kva != NULL) {
	// 	palloc_free_page(page->frame->kva);
	// }
}
