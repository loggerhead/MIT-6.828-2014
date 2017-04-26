// program to cause a breakpoint trap

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
    asm volatile("int $3");
    int a = 1;
    int b = 2;
    int c = a + b;
    cprintf("==> %d\n", c);
}
