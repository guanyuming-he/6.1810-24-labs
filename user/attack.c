#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // your code here.  you should write the secret to fd 2 using write
  // (e.g., write(2, secret, 8)

  // In attacktest, after the secret process is waited to exit,
  // the kernel has freed all pages from it, adding them to kmem->freelist,
  // and say the secret is the Xth page before the end.
  // The wait is needed for the process to be freed (proc.c:425), which is done
  // through freeproc() (proc.c:160), in which 
  // 1. its trapframe is first freed, yielding 1 page.
  // 2. the pagetable is freed via proc_freepagetable() (proc.c:215), which
  // 	- calls two uvmunmap to unmap both the trampoline and the trapframe
  // 	from the process's page table, but does not free them (last arg = 0).
  // 	This is because,
  // 	the trampoline does not belong to this process (see section 4 for why),
  // 	so no needing to be freed, while the trapframe is already freed.
  // 	- calls uvmfree to free the rest pages in the pagetable.
  // 3. Note that the proc struct itself is statically allocated in an array
  // (i.e. stored in kernel data), so no new pages comes from here.
  // 4. So, in total, the number of pages freed is the same as those allocated
  // to the page table.
  //
  // Then, attacktest does these things in order:
  // 1. create a pipe(), which will call pipealloc() (sysfile.c:486),
  // where the 2 new fds do not involve new page allocation, and only the pi
  // is allocated as a page (pipe.c:31).
  // 2. fork() the attacktest process, which copies the whole memory of
  // attacktest (proc.c:296), containing, say, N pages.  
  // 3. Then, in the forked child, exec() into attack.
  // 	Note that exec() allocates M pages for attack first,
  // 	before freeing N-1(trapframe) pages used in fork's parent. Trapframe is
  // 	not freed, because it's reused. The reason is explained in
  // 	the book section 3.8.
  //
  // Assert X > 1(pipe) + N(fork) + M(exec), because otherwise the page holding the content
  // would go into attacktest or attack's text, data, stack, heap, and may be
  // completely unexploitable.  
  // Then, we need to allocate enough pages so that the new pages allocated
  // (plus those allocated for the page table pages) reach X.
  //
  // Now the problem is to determine X and M. N is unneeded because fork frees
  // the N-1 pages after the new process is successfully created.
  // There are several ways to determine X and M:
  // 1. We could directly look into the ELF files of M and N and determine how
  // many pages are needed for them, plus any more used for specific system
  // calls (e.g. sbrk, pipe).
  // 2. Because the allocated vm, except for the trapframe at the top, is
  // continuous (yes, even the guard page in between is allocated, just
  // cleared the PTE_U flag, see the book sec 3.6), we could add a debug
  // backdoor in freeproc (and in exec) that prints how many pages are freed.
  // The number should be 1 (trapframe) + PGROUNDUP(sz)/PGSIZE + pages freed by
  // freewalk (page table pages). Note that in exec the trapframe is neither
  // freed nor alloced, but reused.
  // This is much easier than 1.  Just make sure to delete the backdoors after I
  // figure out the numbers.
  //
  // The output of the backdoors is:
  // exec:23 pages freed for proc sh
  // exec:6 pages alloc for proc attacktest
  // exec:6 pages freed for proc attacktest
  // exec:6 pages alloc for proc secret
  // 39 pages freed for proc secret
  // exec:6 pages freed for proc attacktest
  // exec:6 pages alloc for proc attack
  // We can see that attacktest is freed by exec twice, once before forking and
  // executing secret, and another before forking and executing attacktest.
  // Thus, M=6, X=39 - (1+(6-3)+10) (trapframe + (exec-three page table pages,
  // which are freed last) + 10th sbrk).
  // Note that 39 matches 1+6+ 32(arg for sbrk)
  // Also, although N is not needed, we can also get N=6+1(trapframe).
  // 
  // To determine how many pages to alloc, note that X < 512 (number of pages a
  // table entry contains), thus no new page table page is allocated, so we need
  // to allocate exactly
  // X - M- 1(pipe) - 1(trapframe) pages,
  // which is 17.
  //
  char* mem = sbrk(PGSIZE*17);
  mem = mem+16*PGSIZE; // 17th page.
  const char* secret = mem+32;

  fprintf(2, "%s", secret);

  exit(1);
}
