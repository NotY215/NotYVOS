/* NOTYVOS init — freestanding user program, raw syscalls only. */

typedef unsigned long u64;

static inline long sys_call1(long nr, long a1)
{
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}
static inline long sys_call3(long nr, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

static void write(const char* s, unsigned long n)
{
    sys_call3(1 /*write*/, 1 /*fd*/, (long)s, (long)n);
}

static void print(const char* s)
{
    unsigned long n = 0;
    while (s[n])
        n++;
    write(s, n);
}

static long getpid_(void)
{
    return sys_call1(3 /*getpid*/, 0);
}
static void yield_(void)
{
    sys_call1(2 /*yield*/, 0);
}
static void exit_(int c)
{
    sys_call1(0 /*exit*/, c);
}

void _start(void)
{
    print("init: hello from ring 3\n");

    long pid = getpid_();
    char buf[64];
    int i = 0;
    buf[i++] = 'i';
    buf[i++] = 'n';
    buf[i++] = 'i';
    buf[i++] = 't';
    buf[i++] = ':';
    buf[i++] = ' ';
    buf[i++] = 'p';
    buf[i++] = 'i';
    buf[i++] = 'd';
    buf[i++] = '=';
    if (pid == 0)
        buf[i++] = '0';
    else
    {
        char tmp[20];
        int n = 0;
        while (pid)
        {
            tmp[n++] = '0' + (pid % 10);
            pid /= 10;
        }
        for (int k = n - 1; k >= 0; --k)
            buf[i++] = tmp[k];
    }
    buf[i++] = '\n';
    write(buf, (unsigned long)i);

    for (int k = 0; k < 3; ++k)
    {
        print("init: yielding\n");
        yield_();
    }

    print("init: exiting\n");
    exit_(0);

    for (;;)
    {
    }
}
