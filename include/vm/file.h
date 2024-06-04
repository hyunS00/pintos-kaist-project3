#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page {
	struct file* file; // 매핑된 파일 (파일 타입인 경우)

	off_t offset; // 파일 내 offset (파일 타입인 경우)
	size_t read_bytes; // 읽어야 할 바이트 수 (파일 타입인 경우)
	size_t zero_bytes; // 0으로 채울 바이트 수 (파일 타입인 경우)
	
	size_t swap_slot; // 스왑 슬롯
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
