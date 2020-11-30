#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <kern/syscall.h>
#include <machine/trapframe.h>

#if OPT_A2
#include <vfs.h>
#include <kern/fcntl.h>
int sys_fork(struct trapframe *tf, pid_t *retval) {
	
	// create child proc
	struct proc *proc;

	lock_acquire(PIDLock);
	PIDCounter++;
	lock_release(PIDLock);
	
	proc = proc_create_runprogram("[fork]");
	if (proc == NULL) {
		return SYS_fork;
 	}
	
	lock_acquire(proc->pLock);
	// parent-child relationship
	proc->parent = curproc;
	array_add(curproc->children, proc, NULL);
	
	// copy addrspace
	struct addrspace *child_addrspace;
	int copy_result = as_copy(curproc->p_addrspace, &child_addrspace);
	if (copy_result != 0) {
		return copy_result;
	}
        proc->p_addrspace = child_addrspace;	
	
	// return PID
	*retval = proc->PID;

	// make a copy of the child trapframe
        struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
        *child_tf = *tf;
	
	lock_release(proc->pLock);
	
	// create a thread for the child process
	thread_fork("[child thread]", proc, enter_forked_process, child_tf, 0);
	
	return 0;
}

int sys_execv(const char *program, char **args) {
	struct addrspace *as;
        struct vnode *v;
        vaddr_t entrypoint, stackptr;
        int result;
	
	// Copy the program name into kernel space
	char *kern_program = kmalloc((strlen(program) + 1) * sizeof (char));
	if (kern_program == NULL) {
		return ENOMEM;
	}
	size_t got = 0;
	int err = copyinstr((const_userptr_t) program, kern_program, strlen(program) + 1, &got);
	if (err) {
		return err;
	}
	
	//kprintf("%p\n", (void *)kern_program);
	// Count the number of arguments and copy them into the kernel
	int args_count = 0;
	for (int i = 0; args[i] != NULL ; i++) {
		args_count ++;
	}
	//kprintf("%d\n", args_count);
	
	char **kern_args = kmalloc((args_count + 1) * sizeof(char *));
	for (int i = 0; i <= args_count; i++) {
		if (i == args_count) {
			kern_args[i] = NULL;
		} else {
			kern_args[i] = kmalloc((strlen(args[i]) + 1) * sizeof(char));
			if (kern_args[i] == NULL) {
				return ENOMEM;
			}
			int err = copyin((const_userptr_t) args[i], (void *) kern_args[i], (strlen(args[i]) + 1));
                        if (err) {
                                return err;
                        }
			//kprintf("%s\n", (char *) kern_args[i]);
		}
	}	

        /* Open the file. */
        result = vfs_open(kern_program, O_RDONLY, 0, &v);
        if (result) {
                return result;
        }

        /* Create a new address space. */
        as = as_create();
        if (as == NULL) {
                vfs_close(v);
                return ENOMEM;
        }

        /* Switch to it and activate it. */
	// Get the old addrspace
        struct addrspace *old_as = curproc_setas(as);
        as_activate();

        /* Load the executable. */
        result = load_elf(v, &entrypoint);
        if (result) {
                /* p_addrspace will go away when curproc is destroyed */
                vfs_close(v);
                return result;
        }

        /* Done with the file now. */
        vfs_close(v);

        /* Define the user stack in the address space */
        result = as_define_stack(as, &stackptr);
        if (result) {
                /* p_addrspace will go away when curproc is destroyed */
                return result;
        }
	
	////////////////////////////////////////////////////////////
	// Copy arguments onto the stack
	// First copy argument, then the pointers to these arguments
	
	vaddr_t stack_counter = stackptr;
	//kprintf("%p\n", (void *) stack_counter);
	
	// Need to keep track of pointers to arguments on the stack
	vaddr_t *stack_addr = kmalloc((args_count + 1) * sizeof(vaddr_t));
	if (stack_addr == NULL) {
		return ENOMEM;
	}
	size_t args_total_size = 0;
	for (int i = 0; i < args_count; i++) {
		args_total_size += (strlen(kern_args[i]) + 1) * sizeof(char);
	}
	args_total_size = ROUNDUP(args_total_size, 8);
	stack_counter -= args_total_size;
	vaddr_t start_of_args = stack_counter;
	// Copy arguments onto the stack, then store the address in stack_addr
	//kprintf("%d\n", args_total_size);
	for (int i = 0; i <= args_count; i++) {
		if (i == args_count) {
			stack_addr[i] = (vaddr_t) NULL;
		} else {
			//kprintf("%d\n", i);
			//kprintf("%p\n", (void *) stack_counter);
			//kprintf("%s\n", kern_args[i]);
			size_t args_len = strlen(kern_args[i]) + 1;
			int err = copyout((void *)kern_args[i], (userptr_t) stack_counter, args_len);
			if (err) {
				return err;
			}
			stack_addr[i] = stack_counter;
                        stack_counter += args_len * sizeof(char); 
		}
	} 		
	stack_counter = start_of_args;
	//kprintf("%s\n", "args finished");
	//kprintf("%p\n", (void *)stack_counter);
	// Calculate the address of the pointer array, then copy the pointers one by one
	size_t ptr_array_size = (args_count + 1) * sizeof(vaddr_t);
	ptr_array_size = ROUNDUP(ptr_array_size, 8);
	stack_counter -= ptr_array_size;
	vaddr_t top_of_stack = stack_counter;
	
	//kprintf("%s\n", "pointer array");
	// From this addr aligned by 8, list the pointers
	for (int i = 0; i <= args_count; i++) {
		size_t ptr_size = sizeof(vaddr_t);
		//kprintf("%p\n", (void *) stack_counter);
		int err = copyout((void *) &stack_addr[i], (userptr_t) stack_counter, ptr_size);
		if (err) {
			return err;
		}
		stack_counter += ptr_size;
	}
	
	// destroy old as
	as_destroy(old_as);
	
	// free memory
	kfree(stack_addr);
	kfree(kern_program);
	for (int i = 0; i <= args_count; i++) {
		kfree(kern_args[i]);
	}
	kfree(kern_args);
	//kprintf("%p\n", (void *) top_of_stack);
	/////////////////////////////////////////////////////////////
        /* Warp to user mode. */
        enter_new_process(args_count /*argc*/, (userptr_t) top_of_stack /*userspace addr of argv*/,
                          top_of_stack, entrypoint);

        /* enter_new_process does not return. */
        panic("enter_new_process returned\n");
        return EINVAL;
}

#endif /* OPT_A2 */


/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */
void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);
  
  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);
  
#if OPT_A2
  lock_acquire(p->pLock);
  for (unsigned i = 0; i < array_num(p->children); i++) {
        struct proc* child = array_get(p->children, i);
	lock_acquire(child->pLock);
        if (child != NULL && child->status != Alive) {
		lock_release(child->pLock);
                proc_destroy(child);
        } else {
		child->parent = NULL;       
		lock_release(child->pLock);
	}
  } 
  if (p->parent != NULL) {
	p->exitCode = exitcode;
	p->status = Zombie;
	cv_broadcast(p->p_cv, p->pLock);
	lock_release(p->pLock);
  } else {
	lock_release(p->pLock);
	proc_destroy(p);
  }
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");

}
#else

  (void)exitcode;

  
  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}
#endif /* OPT_A2 */



/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
  *retval = curproc->PID;
#else
  *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }

#if OPT_A2
  lock_acquire(curproc->pLock);
  // check pid is one of the children
  bool isChild = false;
  for (unsigned i = 0; i < array_num(curproc->children); i++) {
        struct proc* child = array_get(curproc->children, i);
        if (pid == child->PID) {
                isChild = true;
		// if child did not exit put parent on hold
  		while (child->status == Alive) {
        		cv_wait(child->p_cv, curproc->pLock);
  		}
  		exitstatus = _MKWAIT_EXIT(child->exitCode);
                break;
        }
  }
  if (isChild == false) {
	lock_release(curproc->pLock);
	*retval = -1;
        return(ESRCH);
  }

  lock_release(curproc->pLock);

#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;

#endif /* OPT_A2 */

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

