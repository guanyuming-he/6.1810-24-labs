//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
  struct spinlock lock;
  int locking;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(long long xx, int base, int sign)
{
  char buf[16];
  int i;
  unsigned long long x;

  if(sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console.
int
printf(char *fmt, ...)
{
  va_list ap;
  int i, cx, c0, c1, c2, locking;
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  va_start(ap, fmt);
  for(i = 0; (cx = fmt[i] & 0xff) != 0; i++){
    if(cx != '%'){
      consputc(cx);
      continue;
    }
    i++;
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0;
    if(c0) c1 = fmt[i+1] & 0xff;
    if(c1) c2 = fmt[i+2] & 0xff;
    if(c0 == 'd'){
      printint(va_arg(ap, int), 10, 1);
    } else if(c0 == 'l' && c1 == 'd'){
      printint(va_arg(ap, uint64), 10, 1);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
      printint(va_arg(ap, uint64), 10, 1);
      i += 2;
    } else if(c0 == 'u'){
      printint(va_arg(ap, int), 10, 0);
    } else if(c0 == 'l' && c1 == 'u'){
      printint(va_arg(ap, uint64), 10, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
      printint(va_arg(ap, uint64), 10, 0);
      i += 2;
    } else if(c0 == 'x'){
      printint(va_arg(ap, int), 16, 0);
    } else if(c0 == 'l' && c1 == 'x'){
      printint(va_arg(ap, uint64), 16, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
      printint(va_arg(ap, uint64), 16, 0);
      i += 2;
    } else if(c0 == 'p'){
      printptr(va_arg(ap, uint64));
    } else if(c0 == 's'){
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
    } else if(c0 == '%'){
      consputc('%');
    } else if(c0 == 0){
      break;
    } else {
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c0);
    }

#if 0
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
#endif
  }
  va_end(ap);

  if(locking)
    release(&pr.lock);

  return 0;
}

void
panic(char *s)
{
#ifdef LAB_TRAPS
	backtrace();
#endif

  pr.locking = 0;
  printf("panic: ");
  printf("%s\n", s);
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}

extern char etext[];  // kernel.ld sets this to end of kernel code.

/**
 * Back trace will print, for every function in the call stack,
 * a line of its virtual address, in the order from the current function to the
 * outmost function, but does not include backtrace itself.
 */
void
backtrace()
{
	printf("backtrace:\n");

	// first, because each process's kstack is always of 1 PGSIZE,
	// we can use ROUNDDOWN(s0) and ROUNDUP(s0) as the page boundaries.
	uint64 s0 = r_fp();
	const uint64 ks_low = PGROUNDDOWN(s0);
	const uint64 ks_high = PGROUNDUP(s0);

	// second, the return address must be between KERNBASE
	// and kernel text end, which is the variable etext.
	const uint64 rt_low = KERNBASE;
	const uint64 rt_high = (uint64)etext;

	// now, for each s0:
	// if s0, stack frame point, is out of [ks_low, ks_high], ir
	// if *(s0-8), ret addr, is out of [rt_low, rt_high],
	// then break.
	// Otherwise,
	// print *(s0-8).
	// s0 <- *(s0-16).
	uint64 rt;
	for(;;)
	{
		// must check this first before using s0 to 
		// assign the value of rt.
		if(s0 < ks_low || s0 > ks_high)
			break;

		// uint64* - 1 is char* - 8.
		// rt <- *(s0-8).
		rt = *((uint64*)s0 - 1);
		if (rt < rt_low || rt > rt_high)
			break;

		printf("%p\n", (void*)rt);
		// s0 <- *(s0-16).
		s0 = *((uint64*)s0 - 2);
	}
}
