/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"
#include "lib/kernel/bitmap.h"

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
	swap_disk = disk_get(1,1);
	size_t bit_cnt = disk_size(swap_disk) / (PGSIZE/ DISK_SECTOR_SIZE);
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
	int swap_index = anon_page->swap_slot;

	// 페이지가 스왑 영역에 존재하지 않는 경우
	if (swap_index == -1) {
		memset(kva, 0, PGSIZE);
		return false;
	}

	// 스왑 영역에서 해당 인덱스의 페이지 데이터를 kva에 읽어옴
	for (int i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++) {
		disk_read(swap_disk, swap_index * PGSIZE / DISK_SECTOR_SIZE + i, kva + DISK_SECTOR_SIZE * i);
	}

	// 스왑 테이블에서 해당 인덱스 비트를 사용 가능으로 설정
	bitmap_set(swap_table, swap_index, false);

	// 페이지 구조체의 스왑 인덱스 초기화
	anon_page->swap_slot = -1;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
    struct anon_page *anon_page = &page->anon;
    size_t swap_slot = bitmap_scan_and_flip(swap_table, 0, 1, false);

    // 사용 가능한 스왑 슬롯이 없는 경우 에러 처리
    if (swap_slot == BITMAP_ERROR) {
        return false;
    }

    anon_page->swap_slot = swap_slot;

    // 스왑 슬롯의 시작 섹터 번호 계산
    size_t sector_no = swap_slot * (PGSIZE / DISK_SECTOR_SIZE);

    // 페이지 데이터를 디스크에 쓰기
    for (int i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++) {
        disk_write(swap_disk, sector_no + i, page->frame->kva + DISK_SECTOR_SIZE * i);
    }

    // 페이지 테이블 엔트리에서 해당 페이지 매핑 해제
    pml4_clear_page(thread_current()->pml4, page->va);

    // 프레임 테이블에서 해당 프레임 제거
    list_remove(&page->frame->frame_elem);

    // 물리 페이지 할당 해제
    // palloc_free_page(page->frame->kva);
	// free(page->frame);
    page->frame = NULL;

    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	uint64_t pml4 = thread_current()->pml4;

	if (pml4_get_page(pml4, page->va) != NULL) {
		pml4_clear_page(pml4, page->va);
		palloc_free_page(page->frame->kva);
		list_remove(&page->frame->frame_elem);
	}
	if (anon_page->swap_slot != -1) {
        bitmap_set(swap_table, anon_page->swap_slot, false);
    }
}