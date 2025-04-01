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
  // and say the secret is the Xth added page.
  // The wait is needed for the process to be freed (proc.c:425), which is done
  // through freeproc() (proc.c:160), in which 
  // 1. its trapframe is first freed, yielding 1 page.
  // 2. the pagetable is freed via proc_freepagetable() (proc.c:215), which
  // 	- calls two uvmunmap to unmap both the trampoline and the trapframe
  // 	from the process's page table, but does not free them (last arg = 0).
  // 	- calls uvmfree to free the rest pages in the pagetable.
  // 3. Note that the proc struct itself is statically allocated in an array
  // (i.e. stored in kernel data), so no new pages comes from here.
  //
  // Then, attacktest does these things in order:
  // 1. create a pipe(), which may cause the kernel to allocate a physical page.
  // 2. fork() the attacktest process, which causes, say, N pages to be
  // allocated.
  // 3. Then, in the forked child, exec() into attack.
  // 	Note that exec() allocates M pages for attack first,
  // 	before releasing the N pages used for fork. The reason is explained in
  // 	the book section 3.8.
  //
  // Suppose X > M + N + 1, because otherwise the page holding the content
  // would go into attacktest or attack's text, data, stack, heap, or protection
  // page, and may be completely unexploitable (e.g. if it's in the protection
  // page).
  //
  // Then, we need to allocate X - (M+N+1) pages and the secret is then on the
  // last page allocated.
  //
  char* mem = sbrk(PGSIZE*32);
  mem = mem + 9*PGSIZE;
  const char* secret = mem+32;

  write(2, secret, 8);

  exit(1);
}
