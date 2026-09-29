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

static i64 parse_int(const char* s)
{
    i64 v = 0;
    int neg = 0;
    if (*s == '-')
    {
        neg = 1;
        ++s;
    }
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (*s - '0');
        ++s;
    }
    return neg ? -v : v;
}

static void cmd_cat(const char* path)
{
    i64 fd = sys_open(path, 0);
    if (fd < 0)
    {
        printf("cat: %s: not found\n", path);
        return;
    }
    char buf[256];
    for (;;)
    {
        i64 n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        sys_write(1, buf, (u64)n);
    }
    sys_close(fd);
}

static void cmd_ls(const char* path)
{
    i64 fd = sys_open(path ? path : "/", 0);
    if (fd < 0)
    {
        printf("ls: %s: not found\n", path ? path : "/");
        return;
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

static void cmd_write(const char* path, const char* text, int append)
{
    i64 fd = sys_open(path, 0);
    if (fd < 0)
    {
        i64 r = sys_create(path);
        if (r < 0)
        {
            printf("write: cannot create %s\n", path);
            return;
        }
        fd = sys_open(path, 0);
        if (fd < 0)
        {
            printf("write: cannot open %s\n", path);
            return;
        }
    }
    const u64 tlen = strlen(text);
    sys_write(fd, text, tlen);
    sys_write(fd, "\n", 1);
    sys_close(fd);
    (void)append;
}

void _start(void)
{
    stdio_init();
    printf("\nNOTYVOS shell (phase 2M+)\n");
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
            printf("commands:\n");
            printf("  help              this message\n");
            printf("  ls [DIR]          list directory\n");
            printf("  cat FILE          print file\n");
            printf("  echo TEXT         echo\n");
            printf("  write FILE TEXT   write to /disk/FILE\n");
            printf("  rm FILE           delete /disk/FILE\n");
            printf("  pid               current pid\n");
            printf("  fork              fork a child\n");
            printf("  exec PATH         replace image\n");
            printf("  brk [N]           heap break\n");
            printf("  time              uptime ms\n");
            printf("  sleep N           sleep N ms\n");
            printf("  kill PID          send SIGTERM\n");
            printf("  exit              quit shell\n");
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
            cmd_ls(argc >= 2 ? argv[1] : "/");
        }
        else if (strcmp(argv[0], "cat") == 0)
        {
            if (argc < 2)
                printf("cat: missing file\n");
            else
                cmd_cat(argv[1]);
        }
        else if (strcmp(argv[0], "write") == 0)
        {
            if (argc < 3)
            {
                printf("usage: write FILE TEXT\n");
                continue;
            }
            char path[160] = "/disk/";
            int pi = 6;
            for (const char* s = argv[1]; *s && pi < 158; ++s)
                path[pi++] = *s;
            path[pi] = 0;
            /* Build text from remaining args */
            char text[256];
            int ti = 0;
            for (int i = 2; i < argc; ++i)
            {
                if (i > 2)
                    text[ti++] = ' ';
                for (const char* s = argv[i]; *s && ti < 254; ++s)
                    text[ti++] = *s;
            }
            text[ti] = 0;
            cmd_write(path, text, 0);
            printf("wrote %d bytes to %s\n", ti, path);
        }
        else if (strcmp(argv[0], "rm") == 0)
        {
            if (argc < 2)
            {
                printf("usage: rm FILE\n");
                continue;
            }
            char path[160] = "/disk/";
            int pi = 6;
            for (const char* s = argv[1]; *s && pi < 158; ++s)
                path[pi++] = *s;
            path[pi] = 0;
            i64 r = sys_unlink(path);
            printf("rm %s -> %d\n", path, (int)r);
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
                i64 want = parse_int(argv[1]);
                i64 got = sys_brk((u64)(cur + want));
                printf("brk %+d -> 0x%x\n", (int)want, (unsigned long)got);
            }
        }
        else if (strcmp(argv[0], "time") == 0)
        {
            printf("uptime = %d ms\n", (int)sys_time());
        }
        else if (strcmp(argv[0], "sleep") == 0)
        {
            i64 ms = 1000;
            if (argc >= 2)
                ms = parse_int(argv[1]);
            printf("sleeping %d ms...\n", (int)ms);
            sys_sleep(ms);
            printf("woke up at %d ms\n", (int)sys_time());
        }
        else if (strcmp(argv[0], "kill") == 0)
        {
            if (argc < 2)
            {
                printf("usage: kill PID\n");
                continue;
            }
            i64 pid = parse_int(argv[1]);
            i64 r = sys_kill(pid, 15);
            printf("kill %d -> %d\n", (int)pid, (int)r);
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
