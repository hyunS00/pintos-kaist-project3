/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "kernel/bitmap.h"
#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/palloc.h"
#include "threads/mmu.h"
#include "vm/anon.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
struct bitmap *swap_table;

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
	/* swap disk get */
	swap_disk = disk_get(1, 1);
	
	/* 한 비트가 하나의 스왑 슬롯이 사용 가능한지 여부를 갖고있다. */
	/* ex. swap disk = 512MB, PGSIZE = 4KB
			- 스왑 디스크에 128,000개의 페이지만큼 쓸 수 있다.
			- 한 비트가 한 스왑 슬롯을 나타내므로,
			- 필요한 바이트 수는 128,000 / 8 = 16,000 바이트이다. */
	/* disk_size는 swap_disk의 sector 개수를 반환한다.
		1. disk_size(swap_disk) * DISK_SECTOR_SIZE == swap disk의 총 바이트 수
		2. disk_size(swap_disk) * DISK_SECTOR_SIZE / PGSIZE == 스왑 슬롯의 개수 */
	size_t bit_cnt = disk_size(swap_disk) * DISK_SECTOR_SIZE / PGSIZE;

	/* swap table */
	swap_table = bitmap_create(bit_cnt);
	/* swap table을 false로 초기화 */
	bitmap_set_all(swap_table, false);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

	/* swap slot 초기화, 
		현재는 swap disk에 없는 상태 */
	anon_page->swap_slot = -1;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
/* kva: 새로 생긴 프레임의 주소 */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	size_t swap_slot = anon_page->swap_slot;

	/* 스왑 영역에 해당 페이지가 없는 경우 
		- 애초에 이쪽으로 들어오면 문제가 있다. */ 
	if (swap_slot == -1) {
		/* 페이지가 스왑되지 않았고, 새로 초기화된다. */
		memset(kva, 0, PGSIZE);
		// true or false?
		return true;
	}

	size_t sec_no = swap_slot * (PGSIZE / DISK_SECTOR_SIZE);
	
	/* disk에서 읽어서 물리 메모리에 쓴다. */
	for (int i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++) {
		disk_read(swap_disk, sec_no + i, kva + DISK_SECTOR_SIZE * i);
	}

	/* 해당 swap_slot을 사용 가능 상태로 만든다. */
	bitmap_set(swap_table, swap_slot, false);

	/* 스왑 디스크에서 물리 메모리로 swap in 되었으므로 
		swap_slot을 -1로 초기화한다. */
	anon_page->swap_slot = -1;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/* swap_disk에서 사용 가능한 영역을 찾는다. */
	size_t swap_slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
	if (swap_slot == BITMAP_ERROR)
		return false;
	anon_page->swap_slot = swap_slot;

	/* disk에 쓰기 위해 sec_no를 찾는다. 
		- 한 개의 페이지를 쓰기 위해 필요한 섹터 개수: PGSIZE / DISK_SECTOR_SIZE = 8개
		- 0, 8, 16, 24...번 섹터부터 쓰기 작업이 8개 단위로 수행된다. */
	size_t sec_no = swap_slot * (PGSIZE / DISK_SECTOR_SIZE);
	for (int i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++) {
		/* kva부터 DISK_SECTOR_SIZE만큼 스왑 디스크의 섹터에 쓰기 작업을 수행한다. */
		disk_write(swap_disk, sec_no + i, page->frame->kva + DISK_SECTOR_SIZE * i);
	}

	/* 물리 메모리에서 swap out 되었으므로 프레임을 해제한다. */
	/* freeing a frame...
		1. drop page-frame mapping
		2. list_remove(frame_table)
		3. palloc free page(frame->kva)
		4. frame free */
	// 매핑 해제
	pml4_clear_page(thread_current()->pml4, page->va);
	// 프레임 리스트에서 삭제
	list_remove(&page->frame->frame_elem);
	/* 프레임 해제 여부? 
		- 뺄 프레임을 고른 후 해제하지 않고 그 위에 내용을 덮어쓴다. 
		- 만약 여기서 free_page를 한다면 호출자(vm_get_frame)에서 get_frame을 해주어야 한다.*/
	// palloc_free_page(page->frame->kva);
	// free frame
	// free(page->frame);
	// NULL로
	page->frame = NULL;
	
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct thread *curr = thread_current();

	/* spt table에서 destroy할 페이지를 삭제한다. */
	// spt_remove_page(&curr->spt, page);

	if (pml4_get_page(curr->pml4, page->va) != NULL) {
		/* 페이지 - 프레임 사이 매핑을 해제한다. */
		pml4_clear_page(curr->pml4, page->va);

		/* frame_table에서 페이지에 매핑된 프레임을 삭제한다. */
		if (page->frame != NULL) {
			palloc_free_page(page->frame->kva);
			list_remove(&page->frame->frame_elem);
		}
	}

	/* 해당 페이지가 스왑 디스크에 존재하는 경우 */
	if (anon_page->swap_slot != -1) {
		/* 그 페이지가 갖고있던 스왑 슬롯을 사용 가능 상태로 만든다. */
		bitmap_set(swap_table, anon_page->swap_slot, false);
	}

}
