/*
 * defs.h - internal runtime definitions
 */

#pragma once

#include <base/stddef.h>
#include <base/list.h>
#include <base/tcache.h>
#include <runtime/thread.h>

/*
 * constant limits
 * TODO: make these configurable?
 */
#define RUNTIME_MAX_THREADS	100000
#define RUNTIME_STACK_SIZE	128 * KB
#define RUNTIME_GUARD_SIZE	128 * KB


/*
 * Trap frame support
 */

/*
 * See the "System V Application Binary Interface" for a full explation of
 * calling and argument passing conventions.
 */

struct thread_tf {
	/* argument registers, can be clobbered by callee */
	uint64_t rdi; /* first argument */
	uint64_t rsi;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;

	/* callee-saved registers */
	uint64_t rbx;
	uint64_t rbp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;

	/* special-purpose registers */
	uint64_t rax;	/* holds return value */
	uint64_t rip;	/* instruction pointer */
	uint64_t rsp;	/* stack pointer */
};

#define ARG0(tf)        ((tf)->rdi)
#define ARG1(tf)        ((tf)->rsi)
#define ARG2(tf)        ((tf)->rdx)
#define ARG3(tf)        ((tf)->rcx)
#define ARG4(tf)        ((tf)->r8)
#define ARG5(tf)        ((tf)->r9)


/*
 * Thread support
 */

enum {
	THREAD_STATE_RUNNING = 0,
	THREAD_STATE_RUNNABLE,
	THREAD_STATE_SLEEPING,
};

struct stack;

struct thread {
	struct thread_tf	tf;
	struct list_node	link;
	struct stack		*stack;
	unsigned int		main_thread:1;
	unsigned int		state;

	/* channel state */
	void			*chan_buf;
	int			chan_closed;
};

typedef void (*runtime_fn_t)(unsigned long arg);

/* assembly helper routines from switch.S */
extern void __pop_tf(struct thread_tf *tf) __noreturn;
extern void __call_runtime(struct thread_tf *tf, runtime_fn_t fn,
			   void *stack, unsigned long arg);


/*
 * Stack support
 */

#define STACK_PTR_SIZE	(RUNTIME_STACK_SIZE / sizeof(uintptr_t))
#define GUARD_PTR_SIZE	(RUNTIME_GUARD_SIZE / sizeof(uintptr_t))

struct stack {
	uintptr_t	usable[STACK_PTR_SIZE];
	uintptr_t	guard[GUARD_PTR_SIZE]; /* unreadable and unwritable */
};

extern __thread struct tcache_perthread stack_pt;

/**
 * stack_alloc - allocates a stack
 *
 * Stack allocation is extremely cheap, think less than taking a lock.
 *
 * Returns an unitialized stack.
 */
static inline struct stack *stack_alloc(void)
{
	return tcache_alloc(&stack_pt);
}

/**
 * stack_free - frees a stack
 * @s: the stack to free
 */
static inline void stack_free(struct stack *s)
{
	return tcache_free(&stack_pt, (void *)s);
}

/**
 * stack_init_to_rsp - sets up an exit handler and returns the top of the stack
 * @s: the stack to initialize
 * @exit_fn: exit handler that is called when the top of the call stack returns
 *
 * Returns the top of the stack as a stack pointer.
 */
static inline uint64_t stack_init_to_rsp(struct stack *s, void (*exit_fn)(void))
{
	uint64_t rsp;

	s->usable[STACK_PTR_SIZE - 1] = (uintptr_t)exit_fn;
	rsp = (uint64_t)&s->usable[STACK_PTR_SIZE - 1];

	/*
	 * The stack must be 16-byte aligned at process entry according to
	 * the System V Application Binary Interface (section 3.4.1).
	 *
	 * The callee assumes a return address has been pushed on the aligned
	 * stack by CALL, so we look for an 8 byte offset.
	 *
	 * In reality, we 32-byte align the stack, anticipating 256-bit YMM
	 * registers.
	 */
	assert(rsp % 16 == 8);
	return rsp;
}


/*
 * init
 */

extern int stack_init_thread(void);
extern int stack_init(void);
extern int sched_init_thread(void);
extern int sched_init(void);
extern void sched_start(void) __noreturn;
extern int thread_spawn_main(thread_fn_t fn, void *arg);
