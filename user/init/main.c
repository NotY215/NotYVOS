#include "libnoty.h"

#define LINE_MAX 256
#define ARG_MAX 16

static char line[LINE_MAX];
static char s_path[160];
static char s_text[256];

static void read_line(void)
{
    for (int i = 0; i < LINE_MAX; ++i)
        line[i] = 0;

    int pos = 0;
    for (;;)
    {
        char c = 0;
        i64 n = sys_read(0, &c, 1);
        if (n <= 0)
        {
            line[pos] = 0;
            return;
        }
        if (c == '\n' || c == '\r')
        {
            line[pos] = 0;
            return;
        }
        if (c == '\b' || c == 127)
        {
            if (pos > 0)
                --pos;
            continue;
        }
        if (pos < LINE_MAX - 1)
            line[pos++] = c;
    }
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

static void print_help(void)
{
    puts("commands:\n");
    puts("  help              this message\n");
    puts("  cls / clear       clear the screen\n");
    puts("  ls [DIR]          list directory\n");
    puts("  cat FILE          print file\n");
    puts("  echo TEXT         echo\n");
    puts("  write FILE TEXT   write to /disk/FILE\n");
    puts("  rm FILE           delete /disk/FILE\n");
    puts("  trunc FILE N      truncate to N bytes\n");
    puts("  chmod FILE MODE   change permissions (octal)\n");
    puts("  pid               current pid\n");
    puts("  fork              fork a child\n");
    puts("  exec PATH         replace image\n");
    puts("  brk [N]           heap break\n");
    puts("  time              uptime ms\n");
    puts("  sleep N           sleep N ms\n");
    puts("  kill PID          send SIGTERM\n");
    puts("  game PATH         launch PS3 ELF via GameRunner\n");
    puts("  dns HOST          resolve a hostname\n");
    puts("  about             system info\n");
    puts("  exit              shell is init; exit is refused\n");
}

static void print_about(void)
{
    puts("NOTYVOS\n");
    puts("NotY215 x86_64 operating system\n");
    puts("Shell: pid ");
    put_int(sys_getpid());
    putc('\n');
    puts("Uptime: ");
    put_int(sys_time());
    puts(" ms\n");
    puts("Storage: NYFS v2 on AHCI SATA, mounted at /disk\n");
    puts("Network: e1000 + DHCP + DNS + Wi-Fi framework\n");
    puts("Bluetooth: HCI + device manager\n");
}

static void cmd_ls(const char* path)
{
    i64 fd = sys_open(path, 0);
    if (fd < 0)
    {
        puts("ls: not found: ");
        puts(path);
        putc('\n');
        return;
    }
    DirEntry e;
    u64 shown = 0;
    for (u64 i = 0;; ++i)
    {
        i64 n = sys_readdir(fd, i, &e);
        if (n <= 0)
            break;
        puts(e.name);
        putc('\n');
        ++shown;
    }
    if (shown == 0)
        puts("(empty)\n");
    sys_close(fd);
}

static void cmd_cat(const char* path)
{
    i64 fd = sys_open(path, 0);
    if (fd < 0)
    {
        puts("cat: not found: ");
        puts(path);
        putc('\n');
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

void _start(void)
{
    stdio_init();

    puts("\nNOTYVOS shell\n");
    puts("type 'help' for commands\n");

    // Self-test: prove the write path works before entering the loop.
    puts("selftest: ok\n");

    for (;;)
    {
        puts("$ ");
        read_line();

        char* argv[ARG_MAX];
        int argc = tokenize(line, argv, ARG_MAX);

        // Diagnostic: always print what was parsed.
        puts("[argc=");
        put_int(argc);
        if (argc > 0)
        {
            puts(" cmd=");
            puts(argv[0]);
        }
        puts("]\n");

        if (argc == 0)
            continue;

        if (strcmp(argv[0], "help") == 0)
        {
            print_help();
        }
        else if (strcmp(argv[0], "cls") == 0 || strcmp(argv[0], "clear") == 0)
        {
            putc(0x0C);
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

            (void)sys_create(s_path);
            i64 fd = sys_open(s_path, 0);
            if (fd < 0)
            {
                puts("write: cannot open\n");
                continue;
            }
            u64 n = strlen(s_text);
            i64 wr = sys_write(fd, s_text, n);
            sys_write(fd, "\n", 1);
            sys_close(fd);
            puts("wrote ");
            put_int((i64)n);
            puts(" bytes -> ");
            put_int(wr);
            putc('\n');
        }
        else if (strcmp(argv[0], "rm") == 0)
        {
            if (argc < 2)
            {
                puts("usage: rm FILE\n");
                continue;
            }
            (void)build_path(argv[1]);
            i64 r = sys_unlink(s_path);
            puts("rm -> ");
            put_int(r);
            putc('\n');
        }
        else if (strcmp(argv[0], "trunc") == 0)
        {
            if (argc < 3)
            {
                puts("usage: trunc FILE N\n");
                continue;
            }
            (void)build_path(argv[1]);
            i64 fd = sys_open(s_path, 0);
            if (fd < 0)
            {
                puts("trunc: cannot open\n");
                continue;
            }
            i64 n = parse_int(argv[2]);
            if (n < 0)
                n = 0;
            i64 r = sys_ftruncate(fd, (u64)n);
            sys_close(fd);
            puts("trunc -> ");
            put_int(r);
            putc('\n');
        }
        else if (strcmp(argv[0], "chmod") == 0)
        {
            if (argc < 3)
            {
                puts("usage: chmod FILE MODE\n");
                continue;
            }
            (void)build_path(argv[1]);
            i64 mode = 0;
            const char* s = argv[2];
            while (*s >= '0' && *s <= '7')
            {
                mode = mode * 8 + (*s - '0');
                ++s;
            }
            i64 r = sys_chmod(s_path, mode);
            puts("chmod -> ");
            put_int(r);
            putc('\n');
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
                puts("[child] pid=");
                put_int(sys_getpid());
                putc('\n');
                sys_exit(42);
            }
            else if (child < 0)
                puts("fork failed\n");
            else
            {
                puts("[parent] forked pid=");
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
            i64 rr = sys_exec(argv[1]);
            if (rr < 0 && argv[1][0] != '/')
            {
                char alt[160];
                int i = 0;
                const char* pfx = "/disk/";
                while (pfx[i])
                {
                    alt[i] = pfx[i];
                    ++i;
                }
                for (int k = 0; argv[1][k] && i < 158; ++k)
                    alt[i++] = argv[1][k];
                alt[i] = 0;
                rr = sys_exec(alt);
            }
            puts("exec failed: ");
            put_int(rr);
            putc('\n');
        }
        else if (strcmp(argv[0], "game") == 0)
        {
            if (argc < 2)
            {
                puts("usage: game PATH\n");
                continue;
            }
            i64 r = sys_game_run(argv[1]);
            puts("game -> ");
            put_int(r);
            putc('\n');
        }
        else if (strcmp(argv[0], "dns") == 0)
        {
            if (argc < 2)
            {
                puts("usage: dns HOST\n");
                continue;
            }
            u32 ip = 0;
            i64 n = sys_dns(argv[1], &ip);
            if (n > 0)
            {
                puts(argv[1]);
                puts(" -> ");
                put_int((ip >> 24) & 0xFF);
                putc('.');
                put_int((ip >> 16) & 0xFF);
                putc('.');
                put_int((ip >> 8) & 0xFF);
                putc('.');
                put_int(ip & 0xFF);
                putc('\n');
            }
            else
                puts("dns: failed\n");
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
            puts("exit: shell is init; system would idle.\n");
        }
        else
        {
            puts("unknown: ");
            puts(argv[0]);
            putc('\n');
        }
    }
}
