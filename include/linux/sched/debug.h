/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_DEBUG_H
#define _LINUX_SCHED_DEBUG_H

/*
 * Various scheduler/task debugging interfaces:
 */

struct task_struct;
struct pid_namespace;

extern void dump_cpu_task(int cpu);

/*
 * Only dump TASK_* tasks. (0 for all tasks)
 */
extern void show_state_filter(unsigned int state_filter);

static inline void show_state(void)
{
	show_state_filter(0);
}

struct pt_regs;

extern void show_regs(struct pt_regs *);

/*
 * TASK is a pointer to the task whose backtrace we want to see (or NULL for current
 * task), SP is the stack pointer of the first frame that should be shown in the back
 * trace (or NULL if the entire call-chain of the task should be shown).
 */
extern void show_stack(struct task_struct *task, unsigned long *sp,
		       const char *loglvl);

extern void sched_show_task(struct task_struct *p);

#ifdef CONFIG_SCHED_DEBUG
struct seq_file;
extern void proc_sched_show_task(struct task_struct *p,
				 struct pid_namespace *ns, struct seq_file *m);
extern void proc_sched_set_task(struct task_struct *p);
#endif

/**
 *  Attach to any functions which should be ignored in wchan output.
 * 
 * __sched和前面的asmlinkage一样都是宏，定义中用了gcc的attributes扩展。
 * 代码的意思比较简单，就是把带有__sched的函数放到.sched.text段。
 * 
 * 上面还有一行注释，解释了为什么要放到.sched.text段，这才是有意思的地方。
 * 说的是，如果不想让函数在waiting channel中显示出来，就该加上__sched。
 * 
 * kernel有个waiting channel，如果用户空间的进程睡眠了，可以查到是停在内
 * 核空间哪个函数中等待的：
 * - cat "/proc/<pid>/wchan"
 * 那显然，.sched.text段的代码是会被wchan忽略的，schedule这个函数是
 * 不会出现在wchan的结果中的。
 */
#define __sched		__section(".sched.text")

/* Linker adds these: start and end of __sched functions */
extern char __sched_text_start[], __sched_text_end[];

/* Is this address in the __sched functions? */
extern int in_sched_functions(unsigned long addr);

#endif /* _LINUX_SCHED_DEBUG_H */
