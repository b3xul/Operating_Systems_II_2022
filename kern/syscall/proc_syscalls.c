#include <types.h>		 // userptr_t, size_t
#include <kern/unistd.h> // STDOUT_FILENO, STDERR_FILENO
#include <kern/errno.h>
#include <lib.h> // kprintf, putch
#include <addrspace.h>
#include <proc.h>
#include <thread.h>
#include <current.h> // curproc
#include <synch.h>	 // semaphore
#include <mips/trapframe.h>

#include <syscall.h>

void sys__exit(int status)
{
	// register struct thread *curthread __asm("$23");	/* s7 register */
	// #define curproc (curthread->t_proc)

	kprintf("The process is about to exit this life with status: %d\n", status);

#if OPT_WAITPID_SYSCALL
	// save exist status so that whoever is waiting for this can retrieve it
	struct proc *p = curproc;

	p->p_exit_code = status;
	/*
	 * Detach from our process. You might need to move this action
	 * around, depending on how your wait/exit works.
	 */
	proc_remthread(curthread);
	V(p->p_sem); // Now even if proc_wait deletes the proc before we do the thread exit, it succeeds, since the proc was detached from the thread
#else
	struct addrspace current_as = proc_getas();
	as_destroy(current_as);
	// as_destroy(curproc->p_addrspace);
#endif

	thread_exit();

	panic("thread_exit returned (should not happen)\n");
	(void)status;
}

#if OPT_WAITPID_SYSCALL

pid_t sys_getpid()
{
	KASSERT(curproc != NULL);
	return curproc->p_pid;
}

pid_t sys_waitpid(pid_t pid, userptr_t status, int options)
{
	(void)options; /* not handled */

	struct proc *proc = find_proc_by_pid(pid);
	if (proc == NULL)
		return -1;

	*(int *)status = proc_wait(proc);

	return pid;
}

static void
call_enter_forked_process(void *tfv, unsigned long dummy)
{
	struct trapframe *tf = (struct trapframe *)tfv;
	(void)dummy;
	enter_forked_process(tf);

	panic("enter_forked_process returned (should not happen)\n");
}

int sys_fork(struct trapframe *ctf, pid_t *retval)
{
	struct trapframe *tf_child;
	struct proc *newp;
	int result;

	KASSERT(curproc != NULL);

	newp = proc_create_runprogram(curproc->p_name);
	if (newp == NULL)
	{
		return ENOMEM;
	}

	/* done here as we need to duplicate the address space
	   of the current process */
	as_copy(curproc->p_addrspace, &(newp->p_addrspace));
	if (newp->p_addrspace == NULL)
	{
		proc_destroy(newp);
		return ENOMEM;
	}

	// TODO: add shared open files: proc_file_table_copy(newp,curproc);
	/* we need a copy of the parent's trapframe */
	tf_child = kmalloc(sizeof(struct trapframe));
	if (tf_child == NULL)
	{
		proc_destroy(newp);
		return ENOMEM;
	}
	memcpy(tf_child, ctf, sizeof(struct trapframe));

	/* TO BE DONE: linking parent/child, so that child terminated
	   on parent exit */

	result = thread_fork(
		curthread->t_name,
		newp,
		call_enter_forked_process,
		(void *)tf_child,
		(unsigned long)0 /*unused*/);

	if (result)
	{
		proc_destroy(newp);
		kfree(tf_child);
		return ENOMEM;
	}

	*retval = newp->p_pid;

	return 0;
}

#endif
/*

 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 *
void
proc_destroy(struct proc *proc)
{

	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 *

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);


	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 *

	* VFS fields *
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	* VM fields *
	if (proc->p_addrspace) {

		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 *
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);
}

*/

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 *
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}
*/