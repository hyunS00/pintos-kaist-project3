/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "string.h"
#include "threads/malloc.h"
#include "threads/mmu.h"


static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
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

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page)
{
    struct file_page *file_page = &page->file;
    lock_acquire(&vm_lock);

    if (page->frame != NULL) {
        if (pml4_is_dirty(thread_current()->pml4, page->va)) {
            file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->offset);
            pml4_set_dirty(thread_current()->pml4, page->va, 0);
        }
        file_close(file_page->file);
        pml4_clear_page(thread_current()->pml4, page->va);
        list_remove(&page->frame->frame_elem);
        palloc_free_page(page->frame->kva);
        
	    lock_release(&vm_lock);
    }
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {

	void *start_addr = addr;
	size_t read_bytes = file_length(file) < length ? file_length(file) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;
    int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1
                                                                  : length / PGSIZE; 
	while (read_bytes > 0 || zero_bytes > 0)
	{
		if(spt_find_page(&thread_current()->spt, addr) != NULL)                  
			return NULL;
		
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct file_page *aux = malloc(sizeof(struct file_page));
		if (aux == NULL)
			return NULL;

		aux->file = file_reopen(file);
		aux->offset = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;
        aux->total_page = total_page_count;
        
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, NULL, aux))
		{
			free(aux);                                        
			return NULL;
		}

        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
	}                                               
	return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
    struct supplemental_page_table *spt = &thread_current()->spt;
    struct page *page;

    page = spt_find_page(spt, addr);
    if(page == NULL) {
        return;
    }
    int total_page = page->file.total_page;
    
    /* 매핑된 페이지들을 찾아 제거 */
    for (int i = 0; i < total_page; i++) {
       
        if(page != NULL){
            file_backed_destroy(page);
		    hash_delete(spt, &page->hash_elem);
            free(page);
        }
        addr += PGSIZE;
        page = spt_find_page(spt, addr);
    }
}