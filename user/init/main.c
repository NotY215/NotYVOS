#include "libnoty.h"

#define LINE_MAX 256
#define ARG_MAX 16

static char line[LINE_MAX];

/* Static buffers. Avoids the stack-initialization pattern that tripped a
 * spurious ud2 in Clang -O2. Also simpler to reason about. */
static char s_path[160];
static char s_text[256];

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
        puts("cat: not found\n");
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
    i64 fd = sys_open(path, 0);
    if (fd < 0)
    {
        puts("ls: not found\n");
        return;
    }
    DirEntry e;
    for (u64 i = 0;; ++i)
    {
        i64 n = sys_readdir(fd, i, &e);
        if (n <= 0)
            break;
        puts(e.name);
        putc('\n');
    }
    sys_close(fd);
}

/* Build "/disk/<name>" into s_path. Returns the length. */
static int build_path(const char* name)
{
    int i = 0;
    const char* pfx = "/disk/";
    while (pfx[i])
    {
        s_path[i] = pfx[i];
        ++i;
    }
    for (int k = 0; name[k] && i < 158; ++k)
        s_path[i++] = name[k];
    s_path[i] = 0;
    return i;
}

static void do_write(const char* path, const char* text)
{
    (void)sys_create(path);

    i64 fd = sys_open(path, 0);
    if (fd < 0)
    {
        puts("write: cannot open\n");
        return;
    }

    u64 n = strlen(text);
    i64 wr = sys_write(fd, text, n);
    sys_write(fd, "\n", 1);
    sys_close(fd);

    puts("wrote ");
    put_int((i64)n);
    puts(" bytes, syscall returned ");
    put_int(wr);
    putc('\n');
}

static void do_rm(const char* path)
{
    i64 r = sys_unlink(path);
    puts("rm -> ");
    put_int(r);
    putc('\n');
}

static void print_help(void)
{
    puts("commands:\n");
    puts("  help              this message\n");
    puts("  ls [DIR]          list directory\n");
    puts("  cat FILE          print file\n");
    puts("  echo TEXT         echo\n");
    puts("  write FILE TEXT   write to /disk/FILE\n");
    puts("  rm FILE           delete /disk/FILE\n");
    puts("  pid               current pid\n");
    puts("  fork              fork a child\n");
    puts("  exec PATH         replace image (hello.elf)\n");
    puts("  brk [N]           heap break\n");
    puts("  time              uptime ms\n");
    puts("  sleep N           sleep N ms\n");
    puts("  kill PID          send SIGTERM\n");
    puts("  about             system info\n");
    puts("  exit              shell is init; exit is refused\n");
}

static void print_about(void)
{
    puts("NOTYVOS\n");
    puts("Phase 3B — window manager\n");
    puts("Desktop: framebuffer compositor, back buffer, PS/2 mouse\n");
    puts("Shell: pid ");
    put_int(sys_getpid());
    putc('\n');
    puts("Uptime: ");
    put_int(sys_time());
    puts(" ms\n");
    puts("Storage: NYFS on AHCI SATA, mounted at /disk\n");
    puts("Keyboard: PS/2 i8042, IRQ1. Serial fallback on COM1.\n");
}

void _start(void)
{
    stdio_init();
    puts("\nNOTYVOS shell (phase 3B)\n");
    puts("type 'help' for commands\n");

    for (;;)
    {
        puts("$ ");
        read_line();

        char* argv[ARG_MAX];
        int argc = tokenize(line, argv, ARG_MAX);
        if (argc == 0)
            continue;

        if (strcmp(argv[0], "help") == 0)
        {
            print_help();
        }
        else if (strcmp(argv[0], "about") == 0)
        {
            print_about();
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
                puts("cat: missing file\n");
            else
                cmd_cat(argv[1]);
        }
        else if (strcmp(argv[0], "write") == 0)
        {
            if (argc < 3)
            {
                puts("usage: write FILE TEXT\n");
                continue;
            }
            (void)build_path(argv[1]);

            int ti = 0;
            for (int i = 2; i < argc; ++i)
            {
                if (i > 2 && ti < 254)
                    s_text[ti++] = ' ';
                const char* s = argv[i];
                for (int k = 0; s[k] && ti < 254; ++k)
                    s_text[ti++] = s[k];
            }
            s_text[ti] = 0;

            do_write(s_path, s_text);
        }
        else if (strcmp(argv[0], "rm") == 0)
        {
            if (argc < 2)
            {
                puts("usage: rm FILE\n");
                continue;
            }
            (void)build_path(argv[1]);
            do_rm(s_path);
        }
        else if (strcmp(argv[0], "pid") == 0)
        {
            puts("pid=");
            put_int(sys_getpid());
            putc('\n');
        }
        else if (strcmp(argv[0], "fork") == 0)
        {
            i64 child = sys_fork();
            if (child == 0)
            {
                puts("[child] hello from fork child, pid=");
                put_int(sys_getpid());
                putc('\n');
                sys_exit(42);
            }
            else if (child < 0)
            {
                puts("fork failed\n");
            }
            else
            {
                puts("[parent] forked child pid=");
                put_int(child);
                putc('\n');
                i32 st = 0;
                i64 reaped = sys_wait(-1, &st);
                puts("[parent] reaped pid=");
                put_int(reaped);
                puts(" status=");
                put_int(st);
                putc('\n');
            }
        }
        else if (strcmp(argv[0], "exec") == 0)
        {
            if (argc < 2)
            {
                puts("usage: exec PATH\n");
                continue;
            }
            i64 r = sys_exec(argv[1]);
            puts("exec failed: ");
            put_int(r);
            putc('\n');
        }
        else if (strcmp(argv[0], "brk") == 0)
        {
            i64 cur = sys_brk(0);
            puts("brk = 0x");
            put_hex((u64)cur);
            putc('\n');
            if (argc >= 2)
            {
                i64 want = parse_int(argv[1]);
                i64 got = sys_brk((u64)(cur + want));
                puts("brk -> 0x");
                put_hex((u64)got);
                putc('\n');
            }
        }
        else if (strcmp(argv[0], "time") == 0)
        {
            puts("uptime = ");
            put_int(sys_time());
            puts(" ms\n");
        }
        else if (strcmp(argv[0], "sleep") == 0)
        {
            i64 ms = 1000;
            if (argc >= 2)
                ms = parse_int(argv[1]);
            puts("sleeping...\n");
            sys_sleep(ms);
            puts("woke at ");
            put_int(sys_time());
            puts(" ms\n");
        }
        else if (strcmp(argv[0], "kill") == 0)
        {
            if (argc < 2)
            {
                puts("usage: kill PID\n");
                continue;
            }
            i64 pid = parse_int(argv[1]);
            i64 r = sys_kill(pid, 15);
            puts("kill -> ");
            put_int(r);
            putc('\n');
        }
        else if (strcmp(argv[0], "exit") == 0)
        {
            /* The shell is init (pid 1). If it exits, the system has no
             * running task and idles forever. Refuse and keep the prompt. */
            puts("exit: this shell is init; the system would idle.\n");
            puts("      Use Ctrl+C to interrupt, or power off the VM.\n");
        }
        else if (strcmp(argv[0], "clear") == 0)
        {
            /* Placeholder. The compositor does not parse ANSI yet, so this
             * does nothing visible. Kept so scripts do not break. */
            puts("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
        }
        else
        {
            puts("unknown: ");
            puts(argv[0]);
            putc('\n');
        }
    }
}
