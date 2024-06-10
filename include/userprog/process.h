#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* to lazy load file-backed page. */
bool
lazy_load_segment(struct page *page, void *aux);

struct load_aux {
	struct file *file;
	size_t offset;
	size_t read_bytes;
	size_t zero_bytes;
};
#endif /* userprog/process.h */
