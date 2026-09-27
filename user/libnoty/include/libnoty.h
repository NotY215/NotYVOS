#pragma once

/* NOTYVOS user-space runtime library.
 *
 * Freestanding, no libc. Every function is either an inline syscall wrapper
 * or a small utility implemented in libnoty/string.c or libnoty/printf.c.
 *
 * Syscall numbers must match kernel/src/syscall/syscall.hpp (nr::*).
 */

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef int i32;
typedef unsigned short u16;
typedef short i16;
typedef unsigned char u8;
typedef signed char i8;

typedef u64 usize;

/* ---------------------------------------------------------------------- */
/* Syscall numbers                                                        */
/* ---------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------- */
/* Raw syscall helpers                                                    */
/* ---------------------------------------------------------------------- */

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

/* ---------------------------------------------------------------------- */
/* Syscall wrappers                                                       */
/* ---------------------------------------------------------------------- */

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

/* mmap uses r10, r8, r9 in addition to rdi, rsi, rdx. Cannot be expressed
 * with __sc3. Wrapper uses a dedicated 6-arg assembly block. */
static inline i64 sys_mmap(void* hint, u64 len, i64 flags)
{
    register i64 r10 __asm__("r10") = flags;
    register i64 r8 __asm__("r8") = -1; /* fd */
    register i64 r9 __asm__("r9") = 0;  /* offset */
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

/* ---------------------------------------------------------------------- */
/* String / memory (libnoty/string.c)                                     */
/* ---------------------------------------------------------------------- */
u64 strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, u64 n);
void* memset(void* p, int c, u64 n);
void* memcpy(void* d, const void* s, u64 n);

/* ---------------------------------------------------------------------- */
/* Output helpers (libnoty/printf.c)                                      */
/* ---------------------------------------------------------------------- */
void putc(char c);
void puts(const char* s);
void put_uint(u64 v);
void put_int(i64 v);
void put_hex(u64 v);

/* printf supports: %s %c %u %d %x %p %%
 * Length modifiers and field widths are not implemented in libnoty. */
void printf(const char* fmt, ...);

/* ---------------------------------------------------------------------- */
/* Convenience types shared with the kernel                               */
/* ---------------------------------------------------------------------- */

/* Matches fs::DirEntry on the kernel side. Field order is significant. */
typedef struct
{
    char name[64];
    u32 type; /* 0=File, 1=Dir, 2=Device (fs::VType) */
    u32 _pad;
    u64 ino;
    u64 size;
} DirEntry;
