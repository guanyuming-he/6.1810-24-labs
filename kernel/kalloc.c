// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
// size of all available physical mem.
static uint64 len = 0;
// size of the approximate size of physical mem per CPU
static uint64 chunk = 0;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} kmem;

// spinlock just assigns lock.name = name with no copy of the str.
// Thus, have a static array here.
// How much mem does it occupy?
// First, 64*8 pointers = 512 bytes
// Then, 10*6 (kmemn) + 54*7 (kmemnn) = 438 bytes
// Together = 950 bytes < 1 KiB.
static const char *lock_names[64] = {
    "kmem0","kmem1","kmem2","kmem3","kmem4","kmem5","kmem6","kmem7",
    "kmem8","kmem9","kmem10","kmem11","kmem12","kmem13","kmem14","kmem15",
    "kmem16","kmem17","kmem18","kmem19","kmem20","kmem21","kmem22","kmem23",
    "kmem24","kmem25","kmem26","kmem27","kmem28","kmem29","kmem30","kmem31",
    "kmem32","kmem33","kmem34","kmem35","kmem36","kmem37","kmem38","kmem39",
    "kmem40","kmem41","kmem42","kmem43","kmem44","kmem45","kmem46","kmem47",
    "kmem48","kmem49","kmem50","kmem51","kmem52","kmem53","kmem54","kmem55",
    "kmem56","kmem57","kmem58","kmem59","kmem60","kmem61","kmem62","kmem63"
};

void
kinit(int id)
{
  if (id > 64)
	  panic("kinit: CPU id > 64");

  // spinlock just assigns lock.name = name with no copy of the str.
  initlock(&kmem.lock, lock_names[id]);

  // Boundary w.r.t. integer division.
  // A few interesting problems to note here:
  // 1. Why chunk = len/NCPU first? Because if we do end + (len*id)/NCPU, then
  //	len*id may overflow, if len is too large. It won't overflow with xv6's
  //	len, but it will for real OS with close to 2^64 theoretical possible
  //	len.
  // 2. OK, but since chunk = len/NCPU will be floored, won't id*chunk be
  // inaccurate?
  //	Yes, but that will at most result in one less page for some
  //	cores. Since one's free_end is the next's free_beg, the calculations of
  //	boundaries will be consistent across cores.
  //		But what about the accumulation of pages lost? The last core will
  //	absorb all by setting free_end = PHYSTOP.
  // 3. What about converting everything to double for calc? Not a good idea
  // since it depends on platform acc of double, whether FPU is enabled at
  //	 kernel init, and double's own limitation (losing acc with > 2^52
  //	 integer).
  if(len == 0)
	  len = PHYSTOP - (uint64)end;
  if (chunk == 0)
	  chunk = len/NCPU;
  uint64 free_beg = (uint64)end + id*chunk;
  uint64 free_end = (uint64)end + (id+1)*chunk;
  // Boundary w.r.t. page granularity.
  // They must be both PGROUNDDOWN: since the range is
  // [begin, end), end_i must equal to beg_{i+1}
  free_beg = PGROUNDDOWN(free_beg);
  free_end = PGROUNDDOWN(free_end);
  if (id == 0)
	  free_beg = PGROUNDUP((unit64)end);
  if (id == NCPU-1)
	  free_end = PHYSTOP;

  freerange((void*)free_beg, (void*)free_end);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
//  p = (char*)PGROUNDUP((uint64)pa_start);
	// I already made sure pa_start is on page boundary.
	p = pa_start;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

// calculate  which CPU list the mem belonged to
	int id = NCPU*((uint64)pa-(uint64)end) / len;
	// since boundary is rounded down, it's possible
	// that pa falls slightly behind the end of the calculated id.
	if (PGROUNDDOWN((uint64)end + (id+1)*chunk) <= pa)
		if (id < NCPU-1) ++id;

  acquire(&kmem.lock[id]);
  r->next = kmem.freelist[id];
  kmem.freelist[id] = r;
  release(&kmem.lock[id]);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc_id(int id)
{
  struct run *r;

  acquire(&kmem.lock[id]);
  r = kmem.freelist[id];
  if(r) {
    kmem.freelist[id] = r->next;
	release(&kmem.lock[id]);
  }
  else {
	release(&kmem.lock[id]);
	// try to steal
	for (int i = 0; i < NCPU; ++i)
	{
		if (i == id) continue;
		acquire(&kmem.lock[i]);
		if (kmem.freelist[i]) {
			r = kmem.freelist[i];
			kmem.freelist[i] = r->next;
			release(&kmem.lock[i]);
			break;
		}
		release(&kmem.lock[i]);
	}
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void *
kalloc(void)
{
	// temporarily disable interrupts
	push_off();
	int id = cpuid();
	pop_off();
	return kalloc_id(id);
}
