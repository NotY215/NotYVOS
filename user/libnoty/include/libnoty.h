#pragma once

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef int i32;
typedef unsigned short u16;
typedef short i16;
typedef unsigned char u8;
typedef signed char i8;
typedef u64 usize;

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_YIELD 2
#define SYS_GETPID 3
#define SYS_OPEN 4
#define SYS_READ 5
#define SYS_CLOSE 6
#define SYS_FORK 7
#define SYS_WAIT 8
#define SYS_READDIR 9
#define SYS_MMAP 10
#define SYS_EXEC 11
#define SYS_BRK 12
#define SYS_TIME 13
#define SYS_SLEEP 14
#define SYS_KILL 15
#define SYS_CREATE 16
#define SYS_UNLINK 17

static inline i64 __sc1(i64 n, i64 a1)
{
    i64 r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return r;
}

static inline i64 __sc3(i64 n, i64 a1, i64 a2, i64 a3)
{
    i64 r;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return r;
}

static inline i64 sys_write(i64 fd, const void* buf, u64 len)
{
    return __sc3(SYS_WRITE, fd, (i64)buf, (i64)len);
}
static inline i64 sys_read(i64 fd, void* buf, u64 len)
{
    return __sc3(SYS_READ, fd, (i64)buf, (i64)len);
}
static inline i64 sys_open(const char* path, i64 flags)
{
    return __sc3(SYS_OPEN, (i64)path, flags, 0);
}
static inline i64 sys_close(i64 fd)
{
    return __sc1(SYS_CLOSE, fd);
}
static inline i64 sys_yield(void)
{
    return __sc1(SYS_YIELD, 0);
}
static inline i64 sys_getpid(void)
{
    return __sc1(SYS_GETPID, 0);
}
static inline i64 sys_fork(void)
{
    return __sc1(SYS_FORK, 0);
}
static inline i64 sys_wait(i64 pid, i32* status)
{
    return __sc3(SYS_WAIT, pid, (i64)status, 0);
}
static inline i64 sys_readdir(i64 fd, u64 idx, void* out)
{
    return __sc3(SYS_READDIR, fd, (i64)idx, (i64)out);
}
static inline i64 sys_exec(const char* path)
{
    return __sc1(SYS_EXEC, (i64)path);
}
static inline i64 sys_brk(u64 addr)
{
    return __sc1(SYS_BRK, (i64)addr);
}
static inline i64 sys_time(void)
{
    return __sc1(SYS_TIME, 0);
}
static inline i64 sys_sleep(i64 ms)
{
    return __sc1(SYS_SLEEP, ms);
}
static inline i64 sys_kill(i64 pid, i64 sig)
{
    return __sc3(SYS_KILL, pid, sig, 0);
}
static inline i64 sys_create(const char* path)
{
    return __sc1(SYS_CREATE, (i64)path);
}
static inline i64 sys_unlink(const char* path)
{
    return __sc1(SYS_UNLINK, (i64)path);
}

static inline i64 sys_mmap(void* hint, u64 len, i64 flags)
{
    register i64 r10 __asm__("r10") = flags;
    register i64 r8 __asm__("r8") = -1;
    register i64 r9 __asm__("r9") = 0;
    i64 ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"((i64)SYS_MMAP), "D"((i64)hint), "S"((i64)len), "d"((i64)0), "r"(r10),
                       "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    (void)__sc1(SYS_EXIT, code);
    for (;;)
    {
    }
}

u64 strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, u64 n);
void* memset(void* p, int c, u64 n);
void* memcpy(void* d, const void* s, u64 n);

void putc(char c);
void puts(const char* s);
void put_uint(u64 v);
void put_int(i64 v);
void put_hex(u64 v);
void printf(const char* fmt, ...);

/* Heap. Allocated via sys_brk. */
void* malloc(u64 size);
void free(void* p);
void* calloc(u64 count, u64 size);
void* realloc(void* p, u64 new_size);

/* stdio-like API. `FILE` is opaque to callers. */
typedef struct File File;
File* fopen(const char* path, const char* mode); /* mode: "r", "w", "a" */
i64 fread(void* buf, u64 size, u64 count, File* f);
i64 fwrite(const void* buf, u64 size, u64 count, File* f);
i64 fclose(File* f);
i64 fprintf(File* f, const char* fmt, ...);

/* Standard streams. Only valid after `stdio_init()`. */
extern File* stdin_;
extern File* stdout_;
extern File* stderr_;
#define stdin stdin_
#define stdout stdout_
#define stderr stderr_

void stdio_init(void);

typedef struct
{
    char name[64];
    u32 type;
    u32 _pad;
    u64 ino;
    u64 size;
} DirEntry;
