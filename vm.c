/**********************************************************************
 * Copyright (c) 2020
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "types.h"
#include "list_head.h"
#include "vm.h"

/**
 * Ready queue of the system
 */
extern struct list_head processes;

/**
 * Currently running process
 */
extern struct process *current;

/**
 * Page Table Base Register that MMU will walk through for address translation
 */
extern struct pagetable *ptbr;


/**
 * The number of mappings for each page frame. Can be used to determine how
 * many processes are using the page frames.
 */
extern unsigned int mapcounts[];


/**
 * alloc_page(@vpn, @rw)
 *
 * DESCRIPTION
 *   Allocate a page frame that is not allocated to any process, and map it
 *   to @vpn. When the system has multiple free pages, this function should
 *   allocate the page frame with the **smallest pfn**.
 *   You may construct the page table of the @current process. When the page
 *   is allocated with RW_WRITE flag, the page may be later accessed for writes.
 *   However, the pages populated with RW_READ only should not be accessed with
 *   RW_WRITE accesses.
 *
 * RETURN
 *   Return allocated page frame number.
 *   Return -1 if all page frames are allocated.
 */
unsigned int alloc_page(unsigned int vpn, unsigned int rw)
{	
	bool rwflag = false;
	if (rw != 1) rwflag = true;

	int op_index = vpn / NR_PTES_PER_PAGE;
	int ip_index = vpn % NR_PTES_PER_PAGE;
	
	if(current->pagetable.outer_ptes[op_index] == NULL){
		current->pagetable.outer_ptes[op_index] = malloc(sizeof(struct pte_directory));
	}

	for(int i = 0; i < NR_PAGEFRAMES; i++){
		if(mapcounts[i] == 0){
			mapcounts[i] += 1;
			current->pagetable.outer_ptes[op_index]->ptes[ip_index].valid = true;
			current->pagetable.outer_ptes[op_index]->ptes[ip_index].writable = rwflag;
			current->pagetable.outer_ptes[op_index]->ptes[ip_index].pfn = i;
			if(rwflag == true){
				current->pagetable.outer_ptes[op_index]->ptes[ip_index].private = 1;
			}
			else{
				current->pagetable.outer_ptes[op_index]->ptes[ip_index].private = 0;
			}
			return i;
		}
	}
	
	return -1;
}


/**
 * free_page(@vpn)
 *
 * DESCRIPTION
 *   Deallocate the page from the current processor. Make sure that the fields
 *   for the corresponding PTE (valid, writable, pfn) is set @false or 0.
 *   Also, consider carefully for the case when a page is shared by two processes,
 *   and one process is to free the page.
 */
void free_page(unsigned int vpn)
{
	int op_index = vpn / NR_PTES_PER_PAGE;
	int ip_index = vpn % NR_PTES_PER_PAGE;

	current->pagetable.outer_ptes[op_index]->ptes[ip_index].valid = false;
	current->pagetable.outer_ptes[op_index]->ptes[ip_index].writable = false;
	current->pagetable.outer_ptes[op_index]->ptes[ip_index].private = 0;
	mapcounts[current->pagetable.outer_ptes[op_index]->ptes[ip_index].pfn] -= 1;
	current->pagetable.outer_ptes[op_index]->ptes[ip_index].pfn = 0;

}


/**
 * handle_page_fault()
 *
 * DESCRIPTION
 *   Handle the page fault for accessing @vpn for @rw. This function is called
 *   by the framework when the __translate() for @vpn fails. This implies;
 *   0. page directory is invalid
 *   1. pte is invalid
 *   2. pte is not writable but @rw is for write
 *   This function should identify the situation, and do the copy-on-write if
 *   necessary.
 *
 * RETURN
 *   @true on successful fault handling
 *   @false otherwise
 */
bool handle_page_fault(unsigned int vpn, unsigned int rw)
{
	int op_index = vpn / NR_PTES_PER_PAGE;
	int ip_index = vpn % NR_PTES_PER_PAGE;
	
	if(current->pagetable.outer_ptes[op_index]->ptes[ip_index].private == 1){
		if(mapcounts[current->pagetable.outer_ptes[op_index]->ptes[ip_index].pfn] == 1){
			current->pagetable.outer_ptes[op_index]->ptes[ip_index].writable = true;
			return true;
		}
		else{
			for(int i = 0; i < NR_PAGEFRAMES; i++){
				if(mapcounts[i] == 0){
					mapcounts[i] += 1;
					current->pagetable.outer_ptes[op_index]->ptes[ip_index].writable = true;
					mapcounts[current->pagetable.outer_ptes[op_index]->ptes[ip_index].pfn] -= 1;
					current->pagetable.outer_ptes[op_index]->ptes[ip_index].pfn = i;
					return true;
				}
			}
		}
	}
	return false;
}


/**
 * switch_process()
 *
 * DESCRIPTION
 *   If there is a process with @pid in @processes, switch to the process.
 *   The @current process at the moment should be put into the @processes
 *   list, and @current should be replaced to the requested process.
 *   Make sure that the next process is unlinked from the @processes, and
 *   @ptbr is set properly.
 *
 *   If there is no process with @pid in the @processes list, fork a process
 *   from the @current. This implies the forked child process should have
 *   the identical page table entry 'values' to its parent's (i.e., @current)
 *   page table. 
 *   To implement the copy-on-write feature, you should manipulate the writable
 *   bit in PTE and mapcounts for shared pages. You may use pte->private for 
 *   storing some useful information :-)
 */
 
void switch_process(unsigned int pid)
{
	struct process *p = NULL;
	struct list_head *ptr, *ptrn;

	list_for_each_safe(ptr, ptrn, &processes){
		p = list_entry(ptr, struct process, list);
		if(p->pid == pid){
			list_add_tail(&current->list,&processes);
			list_del_init(&p->list);
			current = p;
			ptbr = &p->pagetable;
			return ;
		}
	}

	p = malloc(sizeof(struct process));
	p->pid = pid;

	for(int j=0; j< NR_PTES_PER_PAGE; j++){
		if(current->pagetable.outer_ptes[j] != NULL){
			p->pagetable.outer_ptes[j] = malloc(sizeof(struct pte_directory));
	
			for(int q=0; q< NR_PTES_PER_PAGE; q++){
				if(current->pagetable.outer_ptes[j]->ptes[q].valid == true){
					p->pagetable.outer_ptes[j]->ptes[q].valid = true;
					
					current->pagetable.outer_ptes[j]->ptes[q].writable = false;
					p->pagetable.outer_ptes[j]->ptes[q].writable = false;
					
					p->pagetable.outer_ptes[j]->ptes[q].private = current->pagetable.outer_ptes[j]->ptes[q].private;
					p->pagetable.outer_ptes[j]->ptes[q].pfn = current->pagetable.outer_ptes[j]->ptes[q].pfn;
					mapcounts[current->pagetable.outer_ptes[j]->ptes[q].pfn] += 1;
				}
			}	
		}
	}
		
	list_add_tail(&current->list,&processes);
	current = p;
	ptbr = &p->pagetable;
}
