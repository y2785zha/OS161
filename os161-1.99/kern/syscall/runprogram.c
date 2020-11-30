/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include "opt-A2.h"
#include <copyinout.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */

#if OPT_A2
int runprogram(char *progname, int args_count, char **args) 
#else
int
runprogram(char *progname)
#endif
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
#if OPT_A2
	struct addrspace *old_as = curproc_setas(as);
#else
	curproc_setas(as);
#endif	
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
#if OPT_A2
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
                args_total_size += (strlen(args[i]) + 1) * sizeof(char);
        }
        args_total_size = ROUNDUP(args_total_size, 8);
        stack_counter -= args_total_size;
        vaddr_t start_of_args = stack_counter;
        // Copy arguments onto the stack, then store the address in stack_addr
        for (int i = 0; i <= args_count; i++) {
                if (i == args_count) {
                        stack_addr[i] = (vaddr_t) NULL;
                } else {
                        //kprintf("%p\n", (void *) stack_counter);
			size_t args_len = strlen(args[i]) + 1;
			int err = copyout(args[i], (userptr_t) stack_counter, args_len);
                        if (err) {
                                return err;
                        }
                        stack_addr[i] = stack_counter;
			stack_counter += args_len * sizeof(char);
                }
        }

        stack_counter = start_of_args;

        // Calculate the address of the pointer array, then copy the pointers one by one
        size_t ptr_array_size = (args_count + 1) * sizeof(vaddr_t);
        ptr_array_size = ROUNDUP(ptr_array_size, 8);
	stack_counter -= ptr_array_size;
        vaddr_t top_of_stack = stack_counter;

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
	
	/* Warp to user mode. */
        enter_new_process(args_count /*argc*/, (userptr_t)top_of_stack /*userspace addr of argv*/,
                          top_of_stack, entrypoint);
#else
	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);
#endif	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

