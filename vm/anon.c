/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "include/threads/mmu.h"
#include "include/lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_table;

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
	swap_disk = disk_get(1,1);
	size_t bit_cnt = (disk_size(swap_disk) * DISK_SECTOR_SIZE) / PGSIZE;
	swap_table = bitmap_create(bit_cnt);
	bitmap_set_all(swap_table, false);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;

	anon_page->swap_slot = -1;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	size_t sector_no = anon_page->swap_slot * (PGSIZE / DISK_SECTOR_SIZE);

	for(size_t i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++){
		disk_read(swap_disk, sector_no + i, kva + DISK_SECTOR_SIZE * i);
	}
	bitmap_set(swap_table, anon_page->swap_slot,false);
	anon_page->swap_slot = -1;
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	size_t swap_slot = bitmap_scan_and_flip(swap_table,0, 1, false);
	anon_page->swap_slot = swap_slot;
	
	size_t sector_no = swap_slot * (PGSIZE / DISK_SECTOR_SIZE);
	
	for (size_t i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++){
		disk_write(swap_disk, sector_no + i, page->frame->kva + DISK_SECTOR_SIZE * i);
	}

	//palloc_free_page(page->frame->kva);
	pml4_clear_page(thread_current()->pml4, page->va);
	page->frame = NULL;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	uint64_t pml4 = thread_current()->pml4;

	/* 페이지 테이블에 매핑됐다면 매핑 끊기 */
	if(pml4_get_page(pml4, page->va) != NULL) {
		pml4_clear_page(pml4, page->va); // 페이지 테이블 매핑 끊기
		lock_acquire(&vm_lock);
		palloc_free_page(page->frame->kva); // 물리 페이지 할당 해제
		if(page->frame != NULL)
			list_remove(&page->frame->frame_elem); // 프레임 리스트에서 프레임 제거
		lock_release(&vm_lock);
	}
}
