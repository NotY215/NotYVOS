#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/arch/x86_64/serial.hpp>
#include <kernel/fb/console.hpp>
#include <kernel/fs/file.hpp>
#include <kernel/fs/nyfs.hpp>
#include <kernel/fs/vfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/pmm.hpp>
#include <kernel/mm/vmm.hpp>
#include <kernel/proc/elf.hpp>
#include <kernel/proc/fork.hpp>
#include <kernel/sched/scheduler.hpp>
#include <kernel/sched/task.hpp>
#include <kernel/syscall/syscall.hpp>
#include <kernel/syscall/uaccess.hpp>

namespace notyvos::syscall
{

SyscallFrame* g_current_frame = nullptr;

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
            auto* cur = sched::scheduler_current();
            if (!cur || !cur->files)
                return -1;
            auto* f = fs::filetable_get(cur->files, static_cast<i32>(fd));
            if (!f || !f->vnode || !f->vnode->ops || !f->vnode->ops->write)
                return -1;
            const isize n =
                f->vnode->ops->write(f->vnode, tmp, f->offset, static_cast<usize>(chunk));
            if (n < 0)
                return -1;
            f->offset += static_cast<usize>(n);
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

    if (parent->files && child->files)
    {
        for (u32 i = 0; i < fs::kMaxFds; ++i)
        {
            child->files->fds[i] = parent->files->fds[i];
        }
    }
    child->brk_start = parent->brk_start;
    child->brk_current = parent->brk_current;
    child->brk_max = parent->brk_max;

    sched::task_add_child(parent, child);
    sched::scheduler_add(child);
    return static_cast<i64>(child->tid);
}

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
                    (void)copy_to_user(status_ptr, &code, sizeof(code));
                const u32 reaped_tid = c->tid;
                sched::task_remove_child(parent, c);
                c->reaped = true;
                sched::task_destroy(c);
                return static_cast<i64>(reaped_tid);
            }
        }
        if (!has_children)
            return -1;
        sched::scheduler_yield();
    }
}

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

    const uptr base =
        (addr_hint != 0) ? (addr_hint & ~(kPage - 1)) : ((cur->user_hi + kPage - 1) & ~(kPage - 1));

    constexpr u64 kLeaf = mm::page_flags::Present | mm::page_flags::Writable | mm::page_flags::User;

    for (u64 i = 0; i < pages; ++i)
    {
        const uptr phys = mm::PhysicalMemory::allocate_frame();
        if (!phys)
            return -1;
        libk::memset(reinterpret_cast<void*>(phys + kHhdm), 0, kPage);
        if (!mm::VirtualMemory::map_page(base + i * kPage, phys, kLeaf))
        {
            return -1;
        }
    }
    if (base + bytes > cur->user_hi)
        cur->user_hi = base + bytes;
    return static_cast<i64>(base);
}

i64 sys_exec_impl(u64 upath)
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

    cur->cr3 = elf.cr3;
    cur->user_entry = elf.entry;
    cur->user_rsp = elf.stack_top;
    cur->user_lo = elf.user_lo;
    cur->user_hi = elf.user_hi;
    cur->brk_start = 0;
    cur->brk_current = 0;
    cur->brk_max = 0;

    asm volatile("mov %0, %%cr3" ::"r"(elf.cr3) : "memory");

    if (g_current_frame)
    {
        g_current_frame->rip = elf.entry;
        g_current_frame->rsp = elf.stack_top;
        g_current_frame->rflags = 0x202;
        g_current_frame->rax = 0;
    }
    return 0;
}

i64 sys_brk_impl(u64 new_brk)
{
    auto* cur = sched::scheduler_current();
    if (!cur)
        return -1;

    if (cur->brk_start == 0)
    {
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
            libk::memset(reinterpret_cast<void*>(phys + kHhdm), 0, 0x1000);
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

i64 sys_time_impl()
{
    return static_cast<i64>(sched::scheduler_uptime_ticks() * 10);
}

i64 sys_sleep_impl(u64 ms)
{
    if (ms == 0)
        return 0;
    const u64 ticks = (ms + 9) / 10;
    const u64 deadline = sched::scheduler_uptime_ticks() + ticks;
    sched::scheduler_sleep_until(deadline);
    return static_cast<i64>(ms);
}

i64 sys_kill_impl(u64 pid, u64 sig)
{
    auto* t = sched::task_by_tid(static_cast<u32>(pid));
    if (!t)
        return -1;
    switch (sig)
    {
    case 9:
    case 15:
        t->exit_code = -static_cast<i32>(sig);
        t->state = sched::TaskState::Zombie;
        log::write(log::Level::Info, "sig", "killed tid=%llu by sig=%llu",
                   static_cast<unsigned long long>(pid), static_cast<unsigned long long>(sig));
        break;
    case 19:
        t->state = sched::TaskState::Stopped;
        break;
    case 18:
        if (t->state == sched::TaskState::Stopped)
        {
            t->state = sched::TaskState::Ready;
        }
        break;
    default:
        return -1;
    }
    return 0;
}

i64 sys_create_impl(u64 upath)
{
    log::write(log::Level::Info, "create", "upath=0x%llx", static_cast<unsigned long long>(upath));
    char path[256];
    if (!copy_from_user(path, upath, sizeof(path)))
        return -1;
    path[sizeof(path) - 1] = 0;

    const char* name = path;
    if (name[0] == '/')
    {
        const char prefix[] = "/disk/";
        u32 i = 0;
        while (prefix[i] && name[i] == prefix[i])
            ++i;
        if (prefix[i] == 0)
            name += i;
    }
    if (!name[0])
        return -1;
    log::write(log::Level::Info, "create", "stripped name='%s'", name);
    return fs::nyfs_create(name);
}

i64 sys_unlink_impl(u64 upath)
{
    char path[256];
    if (!copy_from_user(path, upath, sizeof(path)))
        return -1;
    path[sizeof(path) - 1] = 0;

    const char* name = path;
    if (name[0] == '/')
    {
        const char prefix[] = "/disk/";
        u32 i = 0;
        while (prefix[i] && name[i] == prefix[i])
            ++i;
        if (prefix[i] == 0)
            name += i;
    }
    if (!name[0])
        return -1;
    return fs::nyfs_unlink(name);
}

} // namespace

extern "C" void syscall_dispatch(SyscallFrame* f) noexcept
{
    g_current_frame = f;
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
    case nr::kExec:
        f->rax = static_cast<u64>(sys_exec_impl(f->rdi));
        break;
    case nr::kBrk:
        f->rax = static_cast<u64>(sys_brk_impl(f->rdi));
        break;
    case nr::kTime:
        f->rax = static_cast<u64>(sys_time_impl());
        break;
    case nr::kSleep:
        f->rax = static_cast<u64>(sys_sleep_impl(f->rdi));
        break;
    case nr::kKill:
        f->rax = static_cast<u64>(sys_kill_impl(f->rdi, f->rsi));
        break;
    case nr::kCreate:
        f->rax = static_cast<u64>(sys_create_impl(f->rdi));
        break;
    case nr::kUnlink:
        f->rax = static_cast<u64>(sys_unlink_impl(f->rdi));
        break;
    default:
        log::write(log::Level::Warn, "syscall", "unknown nr=%llu",
                   static_cast<unsigned long long>(f->rax));
        f->rax = static_cast<u64>(-1);
        break;
    }
    g_current_frame = nullptr;
}

} // namespace notyvos::syscall
