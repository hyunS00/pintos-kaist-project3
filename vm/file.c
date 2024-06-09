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

/* 파일에 대한 정보를 담은 가상 페이지를 생성한다*/
static void file_backed_destroy(struct page *page)
{
	uint64_t pml4 = thread_current()->pml4;
	struct file_page *file_page = &page->file;
	if (pml4_is_dirty(pml4, page->va))
	{
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->offset);
	}
	file_close(file_page->file);
	list_remove(&page->frame->frame_elem);
	pml4_clear_page(pml4, page->va);
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	void *start_addr = addr;
	int count = 0;

	while (length > 0)
	{
		if (spt_find_page(&thread_current()->spt, addr) != NULL)
		{
			break;
		}

		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct file_page *aux = malloc(sizeof(struct file_page));

		if (aux == NULL)
			return false;

		aux->file = file_reopen(file);
		aux->offset = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, NULL, aux))
		{
			free(aux);
			return false;
		}

		length -= page_read_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return start_addr;
}

void do_munmap(void *addr)
{
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page;
	void *curr_addr = addr;

	/* 매핑된 페이지들을 찾아 제거 */
	while ((page = spt_find_page(spt, curr_addr)) != NULL)
	{
		file_backed_destroy(page);
		hash_delete(spt, &page->hash_elem);
		curr_addr += PGSIZE;
	}
}

bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *aux = (struct file_page *)page->uninit.aux;
	struct file *file = aux->file;
	off_t ofs = aux->offset;
	size_t page_read_bytes = aux->read_bytes;
	size_t page_zero_bytes = aux->zero_bytes;

	/* Read file data into the page */
	file_read_at(file, kva, page_read_bytes, ofs);
	// if (file_read_at(file, kva, page_read_bytes, ofs) != (int)page_read_bytes)
	// 	return false;

	/* Clear the remaining bytes */
	memset(kva + page_read_bytes, 0, page_zero_bytes);
	free(aux);

	return true;
}