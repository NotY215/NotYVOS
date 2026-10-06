#include "libnoty.h"

struct File
{
    i64 fd;
    i64 eof;
    char path[128];
};

static struct File _stdin = {0, 0, {0}};
static struct File _stdout = {1, 0, {0}};
static struct File _stderr = {2, 0, {0}};

File* stdin_ = &_stdin;
File* stdout_ = &_stdout;
File* stderr_ = &_stderr;

void stdio_init(void)
{
    stdin_->fd = 0;
    stdout_->fd = 1;
    stderr_->fd = 2;
}

File* fopen(const char* path, const char* mode)
{
    if (!path || !mode)
        return 0;

    i64 flags = 0;
    if (mode[0] == 'w')
        flags = 1; /* we do not delete on w yet */
    else if (mode[0] == 'a')
        flags = 2;

    /* For write modes, create the file first. */
    if (flags != 0)
    {
        sys_create(path);
    }

    i64 fd = sys_open(path, flags);
    if (fd < 0)
        return 0;

    File* f = (File*)malloc(sizeof(File));
    if (!f)
    {
        sys_close(fd);
        return 0;
    }
    f->fd = fd;
    f->eof = 0;
    u64 i = 0;
    while (path[i] && i < 127)
    {
        f->path[i] = path[i];
        ++i;
    }
    f->path[i] = 0;
    return f;
}

i64 fread(void* buf, u64 size, u64 count, File* f)
{
    if (!f || size == 0 || count == 0)
        return 0;
    const u64 want = size * count;
    const i64 got = sys_read(f->fd, buf, want);
    if (got <= 0)
    {
        f->eof = 1;
        return 0;
    }
    return (i64)(got / (i64)size);
}

i64 fwrite(const void* buf, u64 size, u64 count, File* f)
{
    if (!f || size == 0 || count == 0)
        return 0;
    const u64 want = size * count;
    const i64 put = sys_write(f->fd, buf, want);
    if (put <= 0)
        return 0;
    return (i64)(put / (i64)size);
}

i64 fclose(File* f)
{
    if (!f)
        return -1;
    if (f != stdin_ && f != stdout_ && f != stderr_)
    {
        sys_close(f->fd);
        free(f);
    }
    return 0;
}

i64 fprintf(File* f, const char* fmt, ...)
{
    /* Very small subset: %s %d %u %x %c %%
     * We route through printf for now by formatting into a stack buffer
     * and calling sys_write. */
    if (!f)
        return -1;

    char buf[512];
    int n = 0;

    typedef __builtin_va_list va_list;
    va_list ap;
    __builtin_va_start(ap, fmt);

    for (const char* p = fmt; *p && n < (int)sizeof(buf) - 1; ++p)
    {
        if (*p != '%')
        {
            buf[n++] = *p;
            continue;
        }
        ++p;
        switch (*p)
        {
        case 's':
        {
            const char* s = __builtin_va_arg(ap, const char*);
            if (!s)
                s = "(null)";
            while (*s && n < (int)sizeof(buf) - 1)
                buf[n++] = *s++;
            break;
        }
        case 'c':
        {
            char c = (char)__builtin_va_arg(ap, int);
            buf[n++] = c;
            break;
        }
        case 'u':
        {
            unsigned int v = __builtin_va_arg(ap, unsigned int);
            char tmp[24];
            int k = 0;
            if (v == 0)
                tmp[k++] = '0';
            while (v)
            {
                tmp[k++] = (char)('0' + (v % 10));
                v /= 10;
            }
            while (k-- && n < (int)sizeof(buf) - 1)
                buf[n++] = tmp[k];
            break;
        }
        case 'd':
        {
            int v = __builtin_va_arg(ap, int);
            if (v < 0)
            {
                buf[n++] = '-';
                v = -v;
            }
            char tmp[24];
            int k = 0;
            if (v == 0)
                tmp[k++] = '0';
            while (v)
            {
                tmp[k++] = (char)('0' + (v % 10));
                v /= 10;
            }
            while (k-- && n < (int)sizeof(buf) - 1)
                buf[n++] = tmp[k];
            break;
        }
        case 'x':
        {
            u64 v = __builtin_va_arg(ap, u64);
            const char* h = "0123456789abcdef";
            char tmp[20];
            int k = 0;
            if (v == 0)
                tmp[k++] = '0';
            while (v)
            {
                tmp[k++] = h[v & 0xF];
                v >>= 4;
            }
            while (k-- && n < (int)sizeof(buf) - 1)
                buf[n++] = tmp[k];
            break;
        }
        case '%':
            buf[n++] = '%';
            break;
        default:
            buf[n++] = '%';
            buf[n++] = *p;
            break;
        }
    }
    __builtin_va_end(ap);

    if (f->fd == 1 || f->fd == 2)
    {
        sys_write(f->fd, buf, (u64)n);
    }
    else
    {
        sys_write(f->fd, buf, (u64)n);
    }
    return n;
}
