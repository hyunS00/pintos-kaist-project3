/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"
#include "lib/kernel/bitmap.h"
#include "vm/anon.h"

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

bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	/* swap_slot에 올라와 있지 않음 상태를 표현하기 위함*/
	anon_page->swap_slot = -1;

	return true;
}

/* 스왑 디스크에 있는 내용을 물리 메모리로 올린다 */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
	int swap_index = anon_page->swap_slot;

	// 페이지가 스왑 영역에 존재하지 않는 경우
	// if (swap_index == -1)
	// {
	// 	memset(kva, 0, PGSIZE);
	// 	return false;
	// }

	/* 해당 슬롯의 디스크 번호를 반환 */
	disk_sector_t sector_no = swap_index * per_disk_cnt;

	/* 한 페이지는 8개의 디스크 sector에서 읽어야 한다 */
	for (int i = 0; i < per_disk_cnt; i++)
	{
		/* 어느 스왑 디스크에서 읽을 것인지, 디스크의 위치, 올릴 물리 메모리의 주소 */
		disk_read(swap_disk, sector_no + i, kva + DISK_SECTOR_SIZE * i);
	}

	/* 디스크의 해당 슬롯 사용 가능 상태로 */
	bitmap_set(swap_table, swap_index, false);
	/* 해당 페이지는 물리 메모리에 올렸으므로 -1로 설정 */
	anon_page->swap_slot = -1;
	return true;
}

/* 물리 메모리에 있는 내용을 디스크로 내린다 */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
	/* false인 slot을 찾아서 true로 변환한다 */
	size_t swap_slot = bitmap_scan_and_flip(swap_table, 0, 1, false);

	if (swap_slot == BITMAP_ERROR)
	{
		return false;
	}

	/* 해당 slot 번호를 저장한다 1개의 slot이 한 page와 관련이 있다 */
	anon_page->swap_slot = swap_slot;
	/* 한 disk는 8개의 slot과 관련이 있다. -> disk_sector_no를 알아내야 한다 */
	disk_sector_t sector_no = swap_slot * per_disk_cnt;

	for (int i = 0; i < per_disk_cnt; i++)
	{
		/* 디스크에 쓸 장치, 디스크의 어느 위치에 데이터를 쓸 것인지, 메모리에서 데이터를 읽어올 시작 주소 */
		disk_write(swap_disk, sector_no + i, page->frame->kva + DISK_SECTOR_SIZE * i);
	}

	/* not present bit로 변경 */
	pml4_clear_page(thread_current()->pml4, page->va);
	list_remove(&page->frame->frame_elem);
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	uint64_t pml4 = thread_current()->pml4;

	/* 해당 가상 페이지가 연결된 물리 프레임이 없다면 */
	if (pml4_get_page(pml4, page->va) == NULL)
	{
		return;
	}
	/* 해당 페이지 not present로 변경 */
	pml4_clear_page(pml4, page->va);
	/* 물리 메모리 할당 해제 */
	palloc_free_page(page->frame->kva);
	/* 아직 swap out 되지 않았다*/
	if (anon_page->swap_slot == -1)
	{
		/* 물리 메모리 관리 list에서 제거 */
		list_remove(&page->frame->frame_elem);
		return;
	}
	/* swap out 된 page를 제거한다.*/
	/* swap slot 사용 가능함으로 바꿈 */
	bitmap_set(swap_table, anon_page->swap_slot, false);
	return;
}
