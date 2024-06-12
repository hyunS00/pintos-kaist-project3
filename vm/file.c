/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/mmu.h"
#include "include/userprog/process.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* 파일에 대한 정보를 담은 가상 페이지를 제거한다*/
static void file_backed_destroy(struct page *page)
{
	uint64_t pml4 = thread_current()->pml4;
	struct file_page *file_page = &page->file;

	lock_acquire(&vm_lock);
	if (pml4_is_dirty(pml4, page->va))
	{
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->offset);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}

	if (page->frame != NULL)
	{
		list_remove(&page->frame->frame_elem);
	}

	pml4_clear_page(pml4, page->va);
	lock_release(&vm_lock);
	file_close(file_page->file);
}

static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
	struct file *file = file_page->file;
	off_t offset = file_page->offset;
	int page_read_bytes = file_page->read_bytes;
	int page_zero_bytes = file_page->zero_bytes;

	file_seek(file, offset);
	if (file_read(file, kva, page_read_bytes) != (int)page_read_bytes)
	{
		return false;
	}

	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	return true;
}
// static bool
// file_backed_swap_in(struct page *page, void *kva)
// {
// 	// printf("File-backed Swap in!\n");
// 	struct file_page *file_page UNUSED = &page->file;

// 	struct file *file = file_page->file;
// 	size_t offset = file_page->offset;
// 	size_t read_bytes = file_page->read_bytes;
// 	size_t zero_bytes = file_page->zero_bytes;

// 	file_seek(file, offset);
// 	size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
// 	size_t page_zero_bytes = PGSIZE - page_read_bytes;
// 	lock_acquire(&vm_lock);
// 	if (file_read(file, kva, page_read_bytes) != (int)page_read_bytes)
// 		return false;
// 	memset(kva + page_read_bytes, 0, page_zero_bytes);
// 	lock_release(&vm_lock);
// 	return true;
// }

// static bool
// file_backed_swap_out(struct page *page)
// {
// 	struct file_page *file_page UNUSED = &page->file;
// 	struct file *file = file_page->file;

// 	if (file == NULL)
// 	{
// 		return false;
// 	}
// 	if (pml4_is_dirty(thread_current()->pml4, page->va) == true)
// 	{
// 		file_write_at(file, page->frame->kva, file_page->read_bytes, file_page->offset);
// 		pml4_set_dirty(thread_current()->pml4, page->va, 0);
// 	}
// 	list_remove(&page->frame->frame_elem);
// 	// pml4_clear_page(thread_current()->pml4, page->va);
// 	// page->frame = NULL;

// 	return true;
// }
static bool
file_backed_swap_out(struct page *page)
{
	uint64_t pml4 = thread_current()->pml4;
	struct file_page *file_page = &page->file;
	lock_acquire(&vm_lock);
	if (pml4_is_dirty(pml4, page->va))
	{
		file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->offset);
		pml4_set_dirty(pml4, page->va, false);
	}
	list_remove(&page->frame->frame_elem);
	pml4_clear_page(pml4, page->va);
	lock_release(&vm_lock);
	page->frame = NULL; 
	return true;
}

/* length : 가상 메모리에 할당하고자 하는 길이*/
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{

	void *start_addr = pg_round_down(addr);

	int total_page_count;
	/* 할당하고자 하는 길이와 파일의 길이를 비교해서 실제로 읽어야 하는 길이 계산*/
	size_t read_bytes = file_length(file) <= length ? file_length(file) : length;
	/* zero로 설정되는 길이 계산*/
	size_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);
	/* 요청한 length 길이만큼 가상 메모리에 할당하기 위해서 몇 페이지가 필요한지 계산한다*/
	if ((length % PGSIZE) == 0)
	{
		total_page_count = length / PGSIZE;
	}
	else
	{
		total_page_count = (length / PGSIZE) + 1;
	}

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* 이미 해당 주소에 가상 주소가 할당되었을 경우*/
		if (spt_find_page(&thread_current()->spt, addr) != NULL)
		{
			return NULL;
		}

		/* 해당 페이지에서 읽어야 하는 길이*/
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		/* 해당 페이지에서 zero로 설정해야 하는 길이*/
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct file_page *aux = malloc(sizeof(struct file_page));
		if (aux == NULL)
			return false;

		aux->file = file_reopen(file);
		aux->offset = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;
		aux->total_page = total_page_count;

		/* 페이지 초기화 작업*/
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, NULL, aux))
		{
			free(aux);
			return false;
		}
		/* 총 남은 파일 읽어야 하는 바이트*/
		read_bytes -= page_read_bytes;
		/* 총 남은 제로 바이트*/
		zero_bytes -= page_zero_bytes;
		/* 할당해주어야 하는 페이지의 위치를 변경*/
		addr += PGSIZE;
		/* 해당 페이지에서 읽은 만큼 offset 변경*/
		offset += page_read_bytes;
	}
	return start_addr;
}

void do_munmap(void *addr)
{
	struct supplemental_page_table *spt = &thread_current()->spt;

	addr = pg_round_down(addr);

	struct page *page = spt_find_page(spt, addr);
	if (page == NULL)
	{
		return;
	}

	int total_page = page->file.total_page;
	for (int i = 0; i < total_page; i++)
	{
		if (page == NULL)
		{
			return;
		}

		struct file_page *target_file_page = &page->file;

		file_backed_destroy(page);
		hash_delete(spt, &page->hash_elem);
		free(page);

		addr += PGSIZE;
		page = spt_find_page(spt, addr);
	}
}

bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	struct file_page *aux = (struct file_page *)page->uninit.aux;
	memcpy(&page->file, aux, sizeof(struct file_page));

	struct file_page *file_page = &page->file;
	struct file *file = file_page->file;

	off_t offset = file_page->offset;
	size_t page_read_bytes = file_page->read_bytes;
	size_t page_zero_bytes = file_page->zero_bytes;

	ASSERT(page->frame != NULL);
	ASSERT(file != NULL);
	ASSERT(offset >= 0);
	ASSERT(page_read_bytes + page_zero_bytes == PGSIZE);

	// Set up the page operations for file-backed pages
	page->operations = &file_ops;

	// Read file data into the page
	if (file_read_at(file, kva, page_read_bytes, offset) != (int)page_read_bytes)
		return false;

	// Clear the remaining bytes
	memset(kva + page_read_bytes, 0, page_zero_bytes);

	// Free the allocated memory for file_page
	free(aux);

	return true;
}
