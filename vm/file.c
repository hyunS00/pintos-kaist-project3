/* file.c: Implementation of memory backed file object (mmaped object). */
#include "vm/vm.h"
#include "userprog/process.h"
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

bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    page->operations = &file_ops;
    struct file_page *file_page = &page->file;
    struct file_page *aux = (struct file_page *)page->uninit.aux;

    /* file_page에 aux에 담겨있던 내용을 옮겨준다. */
    memcpy(&page->file, aux, sizeof(struct file_page));

    /* file_read를 이곳에서 수행하지 않는 이유? 
        - do_mmap -> vm_alloc_with_initializer에서 lazy_load_segment를 통해 메모리에 로드한다.
        - file_read, memset 과정을 lazy_load_segment가 담당한다.*/

    /* load_segment에서 free(aux) 수행 */
    // free(aux);

    return true;
}

/* Swap in the page by read contents from the file. */
/* 파일 시스템 -> 물리 메모리 */
static bool
file_backed_swap_in (struct page *page, void *kva) {
    // printf("File-backed Swap in!\n");

    struct file_page *file_page UNUSED = &page->file;
    
    struct file *file = file_page->file;
    size_t offset = file_page->offset;
    size_t read_bytes = file_page->read_bytes;
    size_t zero_bytes = file_page->zero_bytes;
	
    file_seek(file, offset);

    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

    lock_acquire(&vm_lock);
	if (file_read(file, kva, page_read_bytes) != (int)page_read_bytes)
		return false;

	memset(kva + page_read_bytes, 0, page_zero_bytes);
	lock_release(&vm_lock);

    return true;
}

/* Swap out the page by writeback contents to the file. */
/* 물리 메모리 -> 파일 시스템 */
static bool
file_backed_swap_out (struct page *page) {
    // printf("File-backed Swap out!\n");

    struct file_page *file_page UNUSED = &page->file;
    size_t offset = file_page->offset;
    size_t read_bytes = file_page->read_bytes;

    struct thread *curr = thread_current();
    
    /* write back */
    if (pml4_is_dirty(curr->pml4, page->va)) {
        lock_acquire(&vm_lock);
        file_write_at(file_page->file, page->frame->kva, read_bytes, offset);
        lock_release(&vm_lock);
        /* write back 되었으므로 더이상 dirty하지 않다. */
        pml4_set_dirty(curr->pml4, page->va, false);
    }
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
    struct file_page *file_page UNUSED = &page->file;
    struct thread *curr = thread_current();

    /* spt table에서 destroy할 페이지를 삭제한다. */
    // 이거 하면 터짐
    // spt_remove_page(&curr->spt, page);
    if (pml4_is_dirty(curr->pml4, page->va)) {
        /* 페이지 단위로 파일을 쪼개놨으므로 그만큼만 write back한다. 
            file_length()를 쓰면 안됨. */
        lock_acquire(&vm_lock);
        file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->offset);
        /* write back 되었으므로 더이상 dirty하지 않다. */
        pml4_set_dirty(curr->pml4, page->va, false);
		pml4_set_accessed(curr->pml4, page->va, false);
        lock_release(&vm_lock);
    }
    /* 파일을 닫는다. */
    lock_acquire(&vm_lock);
    file_close(file_page->file);
    lock_release(&vm_lock);

    /* free page는 호출자가 처리해야 한다. 
    ------ 아래 삭제 */
    /* 연결된 물리 프레임에 대해 */
    if (pml4_get_page(curr->pml4, page->va) != NULL) {
        /* frame_table에서 페이지에 매핑된 프레임을 삭제한다. */
        lock_acquire(&vm_lock);
        list_remove(&page->frame->frame_elem);
        lock_release(&vm_lock);
        /* 가상페이지를 해제하는게 아니라 물리 페이지를 해제해야함 */
        lock_acquire(&vm_lock);
        if (page->frame->kva != NULL) {
			palloc_free_page(page->frame->kva);
		}
        /* 매핑을 삭제한다. */
        pml4_clear_page(thread_current()->pml4, page->va);
        lock_release(&vm_lock);
    }
}

/* mmap flow: 
    page fault! ---> mmap() ---> vm_alloc_page() ---> file_backed_initializer() */
/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
        struct file *file, off_t offset) {
    /* TODO: 유효성 체크는 mmap()에서 */
    
    /* TODO: file_reopen을 몇 번 해줄 것인가?
        (하나의 파일에 대해 여러 페이지가 생성될 때)
        - 한 페이지마다 파일을 reopen할 것인가 (O)
        - 같은 파일에 매핑된 모든 페이지에 대해 파일을 한 번 reopen할 것인가 (X) */
    // file = file_reopen(file);

    /* file_length: 794, length: 4096
        - file의 실제 길이와, 내가 매핑하고자 하는 길이가 다르다. */
    // size_t read_bytes = length;
    size_t read_bytes = file_length(file) < length ? file_length(file) : length;
    // printf("file length: %d, length: %d\n", file_length(file), length);
    size_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);
    int pg_cnt = read_bytes % PGSIZE == 0 ? read_bytes / PGSIZE : read_bytes / PGSIZE + 1;
    /* mmap에 성공하면 파일이 매핑된 가상 주소의 시작 부분을 반환한다. */
    /* TODO: round-down 해주어야 하는가? 
        - 인자로 넘어온 addr은 유저 프로그램이 어디에 매핑하면 좋을지 제안하는 값이다.
        - 따라서 페이지를 할당하기 전에는 round-down된 곳의 addr을 찾아야한다.
        - 마지막에 반환되는 값은 round-down된 addr, 즉 실제로 메모리 어디에 매핑되었는지의 주소이다. */
    addr = pg_round_down(addr);
    void *start_addr = addr;

    while (read_bytes > 0 || zero_bytes > 0) {   
        /* for. mmap-kernel */
        /* addr ~ +PGSIZE만큼 공간에 페이지 할당이 가능한지 확인한다. */
        // if (is_kernel_vaddr(addr + PGSIZE))
        //     return NULL;
        /* 이미 페이지가 있으면 리턴한다. */
        struct page *page = spt_find_page(&thread_current()->spt, addr);
        if (page != NULL) {
            return NULL;
        }
        /* 한 페이지 단위: 
            read byte만큼 파일에서 읽고 남은 부분은 0으로 채워서 PGSIZE를 맞춘다. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;
        struct file_page *aux = malloc(sizeof(struct file_page));
        if (aux == NULL)
            return NULL;
        aux->file = file_reopen(file);
        // aux->file = file;
        aux->offset = offset;
        // aux->read_bytes = read_bytes; ---> merge-mm에서 터짐
        aux->read_bytes = page_read_bytes;
        aux->zero_bytes = page_zero_bytes;
        if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux)) {
            free(aux);
            return NULL;
        }
        // printf("Page 생성됨, 주소: 0x%x\n", addr);
        
        /* 만들어줄 페이지 */
        /* 🚫 not present error reading page in user context.
            - addr을 밑에서 더해주기 전에 find_page를 수행해야 한다. */
        page = spt_find_page(&thread_current()->spt, addr);
        if (page == NULL) {
            return NULL;
        }

        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        /* page마다 어디서부터 읽을지를 offset으로 관리하므로
            ofs를 page_read_byte만큼 올려준다. */
        offset += page_read_bytes;
        /* addr에 위치한 페이지에 한 파일을 매핑하기 위해
            몇 개만큼 페이지를 만들어줬는지 저장해준다. */
        page->pg_cnt = pg_cnt;
    }
    return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
    /* 시작 주소 addr부터 연속적으로 페이지가 한 파일에 매핑되어 있으므로
        첫 페이지에 저장해놓은 pg_cnt만큼 돌면서 매핑을 해제한다. */
    struct thread *curr = thread_current();
    struct page *page = spt_find_page(&curr->spt, addr);
    struct file_page *file_page  = &page->file;
    int pg_cnt = page->pg_cnt;

    for (int i = 0; i < pg_cnt; i++) {
        /* write back */
        if (pml4_is_dirty(curr->pml4, page->va)) {
            /* 페이지 단위로 파일을 쪼개놨으므로 그만큼만 write back한다. 
                file_length()를 쓰면 안됨. */
            lock_acquire(&vm_lock);
            file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->offset);
            lock_release(&vm_lock);
            /* write back 되었으므로 더이상 dirty하지 않다. */
            pml4_set_dirty(curr->pml4, page->va, false);
        }

        /* 동일한 파일 내용이라도 서로 다른 물리 프레임에 매핑된다. 
            따라서 munmap시에 연결된 물리 프레임을 해제해주어야 한다. */
        if (pml4_get_page(curr->pml4, page->va) != NULL) {
            /* frame_table에서 페이지에 매핑된 프레임을 삭제한다. */
            lock_acquire(&vm_lock);
            list_remove(&page->frame->frame_elem);
            /* 매핑을 삭제한다. */
            pml4_clear_page(curr->pml4, page->va);
            lock_release(&vm_lock);
        }
        /* TODO: file_close 관련해서 주가해줌 */
        lock_acquire(&vm_lock);
        hash_delete(&curr->spt, &page->hash_elem);
        lock_release(&vm_lock);
        /* file_reopen을 한 파일에 묶여있는 페이지들에 대해 한 번만 수행했으므로
            file_close는 destroy에서 수행한다. */
        // file_close(file_page->file);
        
		if (page->frame->kva != NULL) {
			palloc_free_page(page->frame->kva);
		}

        /* 다음 페이지 삭제 */ 
        /* page->va가 페이지마다 바뀔 수 있도록 새로 페이지를 찾아주어야한다. */
        addr += PGSIZE;
        page = spt_find_page(&curr->spt, addr);
    }
}