#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/uaccess.hpp>
#include <kernel/sched/scheduler.hpp>
#include <kernel/sched/task.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/fs/file.hpp>
#include <kernel/proc/fork.hpp>
#include <kernel/proc/elf.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/fb/console.hpp>
#include <kernel/log.hpp>

namespace notyvos::syscall
{

namespace
{

constexpr u64 kHhdm = 0xffff800000000000ULL;

void console_put(char c)
{
    arch::x86_64::SerialPort::write_char(c);
    if (fb::Console::ready())
        fb::Console::put(c);
}

i64 sys_write(u64 fd, u64 buf, u64 len)
{
    if (len > 65536)
        return -1;
    char tmp[256];
    u64 done = 0;
    while (done < len)
    {
        const u64 chunk = (len - done > sizeof(tmp)) ? sizeof(tmp) : (len - done);
        if (!copy_from_user(tmp, buf + done, static_cast<usize>(chunk)))
            return -1;
        if (fd == 1 || fd == 2)
        {
            for (u64 i = 0; i < chunk; ++i)
                console_put(tmp[i]);
        }
        else
        {
            return -1;
        }
        done += chunk;
    }
    return static_cast<i64>(len);
}

i64 sys_open(const char* upath, u64 /*flags*/)
{
    if (!upath)
        return -1;
    char path[256];
    for (usize i = 0; i < sizeof(path) - 1; ++i)
    {
        if (!copy_from_user(&path[i], reinterpret_cast<uptr>(upath) + i, 1))
            return -1;
        if (path[i] == 0)
        {
            path[sizeof(path) - 1] = 0;
            goto got;
        }
    }
    path[sizeof(path) - 1] = 0;
got:;
    auto* cur = sched::scheduler_current();
    if (!cur || !cur->files)
        return -1;
    auto* vn = fs::vfs_lookup(path, cur->cwd);
    if (!vn)
        return -1;
    return fs::filetable_alloc(cur->files, vn, 0);
}

i64 sys_read_stdin(u64 ubuf, u64 len)
{
    static bool g_first_stdin = false;
    if (!g_first_stdin)
    {
        g_first_stdin = true;
        log::write(log::Level::Warn, "syscall", "shell is blocked on stdin read");
    }
    usize got = 0;
    while (got < len)
    {
        while (!arch::x86_64::keyboard_has_data())
        {
            asm volatile("sti; hlt");
            while (arch::x86_64::keyboard_poll())
            {
            }
            i32 s;
            while ((s = arch::x86_64::SerialPort::read_char_nonblocking()) >= 0)
            {
                char c = static_cast<char>(s);
                if (c == '\r')
                    c = '\n';
                arch::x86_64::keyboard_inject(c);
            }
        }
        i32 c = arch::x86_64::keyboard_pop();
        if (c < 0)
            continue;
        char ch = static_cast<char>(c);
        if (ch == '\b' || ch == 127)
        {
            if (got > 0)
            {
                got--;
                console_put('\b');
                console_put(' ');
                console_put('\b');
            }
            continue;
        }
        console_put(ch);
        if (!copy_to_user(ubuf + got, &ch, 1))
            return -1;
        got++;
        if (ch == '\n')
            break;
    }
    return static_cast<i64>(got);
}

i64 sys_read(u64 fd, u64 ubuf, u64 len)
{
    if (fd == 0)
        return sys_read_stdin(ubuf, len);
    auto* cur = sched::scheduler_current();
    if (!cur || !cur->files)
        return -1;
    auto* f = fs::filetable_get(cur->files, static_cast<i32>(fd));
    if (!f || !f->vnode || !f->vnode->ops || !f->vnode->ops->read)
        return -1;
    char tmp[256];
    usize total = 0;
    usize remaining = static_cast<usize>(len);
    while (remaining > 0)
    {
        const usize chunk = (remaining < sizeof(tmp)) ? remaining : sizeof(tmp);
        const isize n = f->vnode->ops->read(f->vnode, tmp, f->offset, chunk);
        if (n < 0)
            return -1;
        if (n == 0)
            break;
        if (!copy_to_user(ubuf + total, tmp, static_cast<usize>(n)))
            return -1;
        f->offset += static_cast<usize>(n);
        total += static_cast<usize>(n);
        remaining -= static_cast<usize>(n);
    }
    return static_cast<i64>(total);
}

i64 sys_close(u64 fd)
{
    auto* cur = sched::scheduler_current();
    if (!cur || !cur->files)
        return -1;
    fs::filetable_close(cur->files, static_cast<i32>(fd));
    return 0;
}

i64 sys_yield()
{
    sched::scheduler_yield();
    return 0;
}

i64 sys_getpid()
{
    auto* cur = sched::scheduler_current();
    return cur ? static_cast<i64>(cur->tid) : 0;
}

// ---------------------------------------------------------------------------
// Fork: clone the current task's user memory, files, and hierarchy.
// The child resumes in user mode at the same RIP/RSP with rax=0.
// ---------------------------------------------------------------------------
i64 sys_fork(SyscallFrame* f)
{
    auto* parent = sched::scheduler_current();
    if (!parent || !parent->cr3)
        return -1;

    const uptr new_pml4 = mm::PhysicalMemory::allocate_frame();
    if (!new_pml4)
        return -1;
    auto* dst = reinterpret_cast<u64*>(new_pml4 + kHhdm);
    auto* src = reinterpret_cast<u64*>(parent->cr3 + kHhdm);
    libk::memset(dst, 0, 4096);
    for (u32 i = 256; i < 512; ++i)
        dst[i] = src[i];

    if (!proc::clone_user_range(parent->cr3, new_pml4, parent->user_lo, parent->user_hi))
    {
        return -1;
    }

    auto* child = sched::task_create_forked(parent->name, new_pml4, parent->user_lo,
                                            parent->user_hi, f, sizeof(SyscallFrame));
    if (!child)
        return -1;

    // FileTable copy (shallow: same VNodes, independent offsets).
    if (parent->files && child->files)
    {
        for (u32 i = 0; i < fs::kMaxFds; ++i)
        {
            child->files->fds[i] = parent->files->fds[i];
        }
    }

    sched::task_add_child(parent, child);
    sched::scheduler_add(child);
    return static_cast<i64>(child->tid);
}

// ---------------------------------------------------------------------------
// Wait: block until a child exits. `pid = -1` means any child.
// Returns the child's exit code on success.
// ---------------------------------------------------------------------------
i64 sys_wait(i64 want_pid, u64 status_ptr)
{
    auto* parent = sched::scheduler_current();
    if (!parent)
        return -1;

    for (;;)
    {
        bool has_children = false;
        for (sched::Task* c = parent->first_child; c; c = c->next_sibling)
        {
            has_children = true;
            const bool match = (want_pid < 0) || (static_cast<i64>(c->tid) == want_pid);
            if (match && c->state == sched::TaskState::Zombie)
            {
                const i32 code = c->exit_code;
                if (status_ptr)
                {
                    (void)copy_to_user(status_ptr, &code, sizeof(code));
                }
                const u32 reaped_tid = c->tid;
                sched::task_remove_child(parent, c);
                c->reaped = true;
                // Free the child's Task struct and stack now that we own it.
                sched::task_destroy(c);
                return static_cast<i64>(reaped_tid);
            }
        }
        if (!has_children)
            return -1;
        sched::scheduler_yield();
    }
}

// ---------------------------------------------------------------------------
// Readdir: fill a DirEntry for index `idx` of a directory opened as `fd`.
// Returns 1 on success, 0 when done, -1 on error.
// ---------------------------------------------------------------------------
i64 sys_readdir(u64 fd, u64 idx, u64 out_ptr)
{
    auto* cur = sched::scheduler_current();
    if (!cur || !cur->files)
        return -1;
    auto* f = fs::filetable_get(cur->files, static_cast<i32>(fd));
    if (!f || !f->vnode || !f->vnode->ops)
        return -1;
    if (!f->vnode->ops->readdir)
        return -1;

    fs::DirEntry e{};
    const isize n = f->vnode->ops->readdir(f->vnode, static_cast<usize>(idx), &e);
    if (n <= 0)
        return n;
    if (!copy_to_user(out_ptr, &e, sizeof(e)))
        return -1;
    return 1;
}

// ---------------------------------------------------------------------------
// mmap: anonymous mappings only. fd must be -1. `len` is rounded up to a
// page multiple. `hint` is treated as the desired base; if it is NULL, a
// bump allocator inside [user_hi, user_hi + 256 MB) is used.
// ---------------------------------------------------------------------------
i64 sys_mmap(u64 addr_hint, u64 len, u64 /*prot*/, u64 /*flags*/, i64 fd, u64 /*off*/)
{
    if (fd != -1)
        return -1;
    if (len == 0 || len > 256ULL * 1024 * 1024)
        return -1;

    auto* cur = sched::scheduler_current();
    if (!cur)
        return -1;

    constexpr u64 kPage = 0x1000;
    const u64 pages = (len + kPage - 1) / kPage;
    const u64 bytes = pages * kPage;

    // Simple bump: place mmap region immediately above the current user_hi.
    const uptr base =
        (addr_hint != 0) ? (addr_hint & ~(kPage - 1)) : ((cur->user_hi + kPage - 1) & ~(kPage - 1));

    constexpr u64 kMidFlags = 1ULL | 2ULL | 4ULL;
    constexpr u64 kLeafFlags = 1ULL | 2ULL | 4ULL; // P | W | U

    for (u64 i = 0; i < pages; ++i)
    {
        const uptr phys = mm::PhysicalMemory::allocate_frame();
        if (!phys)
            return -1;
        libk::memset(reinterpret_cast<void*>(phys + kHhdm), 0, kPage);
        if (!mm::VirtualMemory::map_page(base + i * kPage, phys, kLeafFlags))
        {
            return -1;
        }
        (void)kMidFlags;
    }

    if (base + bytes > cur->user_hi)
        cur->user_hi = base + bytes;
    return static_cast<i64>(base);
}

} // namespace

#include <kernel/proc/fork.hpp> // already present

// ---------------------------------------------------------------------------
// exec: replace current process image with the ELF at `upath`.
// ---------------------------------------------------------------------------
i64 sys_exec_impl(SyscallFrame* f, u64 upath)
{
    auto* cur = sched::scheduler_current();
    if (!cur)
        return -1;

    char path[256];
    if (!copy_from_user(path, upath, sizeof(path)))
        return -1;
    path[sizeof(path) - 1] = 0;

    auto* vn = fs::vfs_lookup(path, cur->cwd);
    if (!vn || !vn->ops || !vn->ops->size || !vn->ops->read)
        return -1;

    const isize sz = vn->ops->size(vn);
    if (sz <= 0 || sz > 8 * 1024 * 1024)
        return -1;

    void* elf_bytes = mm::Heap::allocate(static_cast<usize>(sz));
    if (!elf_bytes)
        return -1;

    isize got = 0;
    while (got < sz)
    {
        const isize n = vn->ops->read(vn, static_cast<u8*>(elf_bytes) + got,
                                      static_cast<usize>(got), static_cast<usize>(sz - got));
        if (n <= 0)
            break;
        got += n;
    }
    if (got != sz)
    {
        mm::Heap::deallocate(elf_bytes);
        return -1;
    }

    const auto elf = proc::load_elf(elf_bytes, static_cast<usize>(sz));
    mm::Heap::deallocate(elf_bytes);
    if (!elf.entry)
        return -1;

    // Update task. We leak the old user pages; a proper reclaim lands in a
    // follow-up. This is fine while the shell has bounded memory.
    cur->cr3 = elf.cr3;
    cur->user_entry = elf.entry;
    cur->user_rsp = elf.stack_top;
    cur->user_lo = elf.user_lo;
    cur->user_hi = elf.user_hi;
    cur->brk_start = 0;
    cur->brk_current = 0;
    cur->brk_max = 0;

    // Switch CR3 now so the upcoming iretq lands on the new address space.
    asm volatile("mov %0, %%cr3" ::"r"(elf.cr3) : "memory");

    // Rewrite the syscall return frame to start the new program.
    f->rip = elf.entry;
    f->rsp = elf.stack_top;
    f->rflags = 0x202;
    f->rax = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// brk: extend or shrink the process's heap segment. `new_brk == 0` returns
// the current break without changing anything.
// ---------------------------------------------------------------------------
i64 sys_brk_impl(u64 new_brk)
{
    auto* cur = sched::scheduler_current();
    if (!cur)
        return -1;

    if (cur->brk_start == 0)
    {
        // First call: place the break just above the loaded image.
        cur->brk_start = (cur->user_hi + 0xFFF) & ~0xFFFULL;
        cur->brk_current = cur->brk_start;
        cur->brk_max = cur->brk_start;
    }

    if (new_brk == 0)
        return static_cast<i64>(cur->brk_current);
    if (new_brk < cur->brk_start)
        return static_cast<i64>(cur->brk_current);

    if (new_brk > cur->brk_max)
    {
        const uptr start = cur->brk_max;
        const uptr end = (new_brk + 0xFFF) & ~0xFFFULL;

        constexpr u64 kLeaf =
            mm::page_flags::Present | mm::page_flags::Writable | mm::page_flags::User;

        for (uptr va = start; va < end; va += 0x1000)
        {
            const uptr phys = mm::PhysicalMemory::allocate_frame();
            if (!phys)
                return static_cast<i64>(cur->brk_current);
            libk::memset(reinterpret_cast<void*>(phys + 0xffff800000000000ULL), 0, 0x1000);
            if (!mm::VirtualMemory::map_page(va, phys, kLeaf))
            {
                return static_cast<i64>(cur->brk_current);
            }
        }
        cur->brk_max = end;
        if (cur->user_hi < end)
            cur->user_hi = end;
    }

    cur->brk_current = new_brk;
    return static_cast<i64>(cur->brk_current);
}

extern "C" void syscall_dispatch(SyscallFrame* f) noexcept
{
    switch (f->rax)
    {
    case nr::kExit:
        log::write(log::Level::Info, "user", "sys_exit(%llu) tid=%llu",
                   static_cast<unsigned long long>(f->rdi),
                   static_cast<unsigned long long>(
                       sched::scheduler_current() ? sched::scheduler_current()->tid : 0));
        sched::scheduler_exit_current(static_cast<int>(f->rdi));
        break;
    case nr::kWrite:
        f->rax = static_cast<u64>(sys_write(f->rdi, f->rsi, f->rdx));
        break;
    case nr::kYield:
        f->rax = static_cast<u64>(sys_yield());
        break;
    case nr::kGetPid:
        f->rax = static_cast<u64>(sys_getpid());
        break;
    case nr::kOpen:
        f->rax = static_cast<u64>(sys_open(reinterpret_cast<const char*>(f->rdi), f->rsi));
        break;
    case nr::kRead:
        f->rax = static_cast<u64>(sys_read(f->rdi, f->rsi, f->rdx));
        break;
    case nr::kClose:
        f->rax = static_cast<u64>(sys_close(f->rdi));
        break;
    case nr::kExec:
        f->rax = static_cast<u64>(sys_exec_impl(f, f->rdi));
        break;
    case nr::kBrk:
        f->rax = static_cast<u64>(sys_brk_impl(f->rdi));
        break;
    case nr::kFork:
        f->rax = static_cast<u64>(sys_fork(f));
        break;
    case nr::kWait:
        f->rax = static_cast<u64>(sys_wait(static_cast<i64>(f->rdi), f->rsi));
        break;
    case nr::kReaddir:
        f->rax = static_cast<u64>(sys_readdir(f->rdi, f->rsi, f->rdx));
        break;
    case nr::kMmap:
        f->rax = static_cast<u64>(
            sys_mmap(f->rdi, f->rsi, f->rdx, f->r10, static_cast<i64>(f->r8), f->r9));
        break;
    default:
        log::write(log::Level::Warn, "syscall", "unknown nr=%llu",
                   static_cast<unsigned long long>(f->rax));
        f->rax = static_cast<u64>(-1);
        break;
    }
}

} // namespace notyvos::syscall
