#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/fp-ops.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

#define USERPROG
#define MAX_FDT	128
#define MIN(a, b)	(((a) < (b)) ? (a) : (b))
/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* customed */
	int original_priority;				/* original priority (for donation) */
	int64_t time_to_wakeup; 			/* time to wakeup */
	struct lock *wait_on_lock;			/* wait on lock that points the lock which a thread holds. */
	struct list donations;				/* donations that points d_elem donors. */
	struct list_elem d_elem;			/* List donors element. */

	int nice;							/* nice fields */
	fp_float recent_cpu;				/* recent_cpu  */
	struct list_elem adv_elem;			/* for list all threads */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	/* Project2 - process hierarchy */
	struct thread *parent_process;		/* Parent process */
	struct list child_list;				/* List of children */
	struct list_elem child_elem;		/* Children elem */
	int exit_code;						/* Exit code */

	/* Project2 - File Descriptor */
	int nex_fd;
	// struct file *fdt[MAX_FDT];			/* maximum size: 64 */
	struct file **fdt;				/* File Descriptor Table Pointer */
	struct file *fp;					/* file pointer at running file */

	/* Project2 - process */
	bool terminated;					/* boolean thread */
	struct semaphore sema_exit;			/* semaphore for exit */
	struct semaphore sema_load;			/* semaphore for load */
	struct semaphore sema_wait;			/* semaphore for wait */
	struct intr_frame copied_if;		/* copied intr frame */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

/* customed */
void calculate_recent_cpu(struct thread *t);
void recent_cpu_add_1(void);
void recalculate_priority(void);
void recalculate_recent_cpu(void);
void calculate_priority(struct thread *t);
void calculate_load_avg(void);
void preemption(void);
void thread_sleep(int64_t tick);
void thread_wakeup(int64_t tick);
bool list_higher_priority (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
struct thread* get_thread(tid_t tid);
#endif /* threads/thread.h */