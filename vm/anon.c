/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"
#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct bitmap *swap_table;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);
static int per_disk_cnt = PGSIZE / DISK_SECTOR_SIZE;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	swap_disk = disk_get(1, 1);

	size_t bit_cnt = disk_size(swap_disk) / (PGSIZE / DISK_SECTOR_SIZE);
	swap_table = bitmap_create(bit_cnt);
	bitmap_set_all(swap_table, false);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	/* swap slot에 올라와 있지 않음을 표시하기 위함*/
	anon_page->swap_slot = -1;

	return true;
}

/* 디스크에 있는 내용을 물리 메모리로 올린다*/
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;

	// 스왑 슬롯
	size_t swap_slot = anon_page->swap_slot;
	/* 해당 슬롯의 디스크 번호를 반환*/
	disk_sector_t disk_no = swap_slot * per_disk_cnt;

	/* 한 페이지는 8개의 디스크 sector에서 읽어야 한다*/
	for (int i = 0; i < per_disk_cnt; i++)
	{
		/* 어느 스왑 디스크에서 읽을 것인지,  디스크의 위치, 올릴 물리 메모리의 주소 */
		disk_read(swap_disk, disk_no + i, kva + DISK_SECTOR_SIZE * i);
	}
	/* 해당 페이지는 물리 메모리에 올렸으므로 -1로 설정*/
	anon_page->swap_slot = -1;
	/* 디스크의 해당 슬롯 사용 가능 상태로*/
	bitmap_set(swap_disk, swap_slot, false);
	return true;
}

/* 물리 메모리에 있는 내용을 디스크로 내린다*/
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
	/* false인 slot을 찾아서 true로 변환한다*/
	size_t swap_slot = bitmap_scan_and_flip(swap_disk, 0, 1, false);

	if (swap_slot = BITMAP_ERROR)
	{
		return false;
	}

	/* 해당 slot 번호를 저장한다 1개의 slot이 한 page와 관련이 있다*/
	anon_page->swap_slot = swap_slot;
	/* 한 disk는 8개의 slot과 관련이 있다. -> disk_sector_no를 알아내야 한다*/
	disk_sector_t disk_no = swap_slot * per_disk_cnt;

	for (int i = 0; i < per_disk_cnt; i++)
	{
		/* 디스크에 쓸 장치, 디스크의 어느 위치에 데이터를 쓸 것인지, 메모리에서 데이터를 읽어올 시작 주소*/
		disk_write(swap_disk, disk_no * per_disk_cnt + i, page->frame->kva + DISK_SECTOR_SIZE * i);
	}
	pml4_clear_page(thread_current()->pml4, page->va);
	palloc_free_page(page->frame->kva);
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	uint64_t pml4 = thread_current()->pml4;
	pml4_clear_page(pml4, page->va);

	// frame 제거
	if (page->frame != NULL)
	{
		lock_acquire(&vm_lock);
		palloc_free_page(page->frame->kva);
		list_remove(&page->frame->frame_elem);
		lock_release(&vm_lock);
	}
}
