#include "libnoty.h"

void _start(void)
{
    printf("hello.elf: exec worked\n");
    printf("hello.elf: pid=%d\n", (int)sys_getpid());
    printf("hello.elf: exiting now\n");
    sys_exit(0);
}
