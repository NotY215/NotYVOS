#include "libnoty.h"

#define LINE_MAX 256
#define ARG_MAX 16

static char line[LINE_MAX];

static void read_line(void)
{
    i64 n = sys_read(0, line, LINE_MAX - 1);
    if (n <= 0)
    {
        line[0] = 0;
        return;
    }
    line[n] = 0;
    if (n > 0 && line[n - 1] == '\n')
        line[n - 1] = 0;
}

static int tokenize(char* s, char** argv, int max)
{
    int argc = 0;
    while (*s)
    {
        while (*s == ' ' || *s == '\t')
            *s++ = 0;
        if (!*s)
            break;
        if (argc >= max)
            break;
        argv[argc++] = s;
        while (*s && *s != ' ' && *s != '\t')
            s++;
    }
    return argc;
}

static void cmd_cat(const char* path)
{
    i64 fd = sys_open(path, 0);
    if (fd < 0)
    {
        printf("cat: %s: not found\n", path);
        return;
    }
    char buf[128];
    for (;;)
    {
        i64 n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        sys_write(1, buf, (u64)n);
    }
    sys_close(fd);
}

void _start(void)
{
    printf("\nNOTYVOS shell (phase 2I+)\n");
    printf("type 'help' for commands\n");

    for (;;)
    {
        printf("$ ");
        read_line();
        char* argv[ARG_MAX];
        int argc = tokenize(line, argv, ARG_MAX);
        if (argc == 0)
            continue;

        if (strcmp(argv[0], "help") == 0)
        {
            printf("commands: help, ls, cat FILE, echo TEXT, pid, fork,\n");
            printf("          exec PATH, brk [N], exit\n");
        }
        else if (strcmp(argv[0], "echo") == 0)
        {
            for (int i = 1; i < argc; ++i)
            {
                if (i > 1)
                    putc(' ');
                puts(argv[i]);
            }
            putc('\n');
        }
        else if (strcmp(argv[0], "ls") == 0)
        {
            i64 fd = sys_open(argc >= 2 ? argv[1] : "/", 0);
            if (fd < 0)
            {
                printf("ls: not found\n");
                continue;
            }
            DirEntry e;
            for (u64 i = 0;; ++i)
            {
                i64 n = sys_readdir(fd, i, &e);
                if (n <= 0)
                    break;
                printf("%s\n", e.name);
            }
            sys_close(fd);
        }
        else if (strcmp(argv[0], "cat") == 0)
        {
            if (argc < 2)
                printf("cat: missing file\n");
            else
                cmd_cat(argv[1]);
        }
        else if (strcmp(argv[0], "pid") == 0)
        {
            printf("pid=%d\n", (int)sys_getpid());
        }
        else if (strcmp(argv[0], "fork") == 0)
        {
            i64 child = sys_fork();
            if (child == 0)
            {
                printf("[child pid=%d] hello from fork child\n", (int)sys_getpid());
                sys_exit(42);
            }
            else if (child < 0)
            {
                printf("fork failed\n");
            }
            else
            {
                printf("[parent] forked child pid=%d\n", (int)child);
                i32 st = 0;
                i64 reaped = sys_wait(-1, &st);
                printf("[parent] reaped pid=%d status=%d\n", (int)reaped, st);
            }
        }
        else if (strcmp(argv[0], "exec") == 0)
        {
            if (argc < 2)
            {
                printf("usage: exec PATH\n");
                continue;
            }
            i64 r = sys_exec(argv[1]);
            printf("exec failed: %d\n", (int)r);
        }
        else if (strcmp(argv[0], "brk") == 0)
        {
            i64 cur = sys_brk(0);
            printf("brk = 0x%x\n", (unsigned long)cur);
            if (argc >= 2)
            {
                i64 want = 0;
                for (const char* p = argv[1]; *p >= '0' && *p <= '9'; ++p)
                {
                    want = want * 10 + (*p - '0');
                }
                i64 got = sys_brk((u64)(cur + want));
                printf("brk + %d -> 0x%x\n", (int)want, (unsigned long)got);
            }
        }
        else if (strcmp(argv[0], "exit") == 0)
        {
            printf("bye\n");
            sys_exit(0);
        }
        else
        {
            printf("unknown: %s\n", argv[0]);
        }
    }
}
