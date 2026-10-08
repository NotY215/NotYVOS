#include <kernel/arch/x86_64/isr.hpp>
#include <kernel/arch/x86_64/keyboard.hpp>
#include <kernel/arch/x86_64/mouse.hpp>
#include <kernel/arch/x86_64/pic.hpp>
#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/gfx/compositor.hpp>
#include <kernel/log.hpp>
#include <kernel/panic.hpp>
#include <kernel/sched/scheduler.hpp>

namespace notyvos::arch::x86_64
{

// Defined in kernel/src/net/e1000.cpp. Called from the PIT IRQ so the
// NIC's RX ring is drained 100 times per second without a task.
extern "C" void notyvos_e1000_poll() noexcept;

namespace
{

const char* exception_name(u64 v) noexcept
{
    switch (v)
    {
    case 0:
        return "divide error";
    case 1:
        return "debug";
    case 2:
        return "NMI";
    case 3:
        return "breakpoint";
    case 4:
        return "overflow";
    case 5:
        return "bound range";
    case 6:
        return "invalid opcode";
    case 7:
        return "device not available";
    case 8:
        return "double fault";
    case 10:
        return "invalid TSS";
    case 11:
        return "segment not present";
    case 12:
        return "stack-segment fault";
    case 13:
        return "general protection";
    case 14:
        return "page fault";
    case 16:
        return "x87 FP";
    case 17:
        return "alignment check";
    case 18:
        return "machine check";
    case 19:
        return "SIMD FP";
    case 20:
        return "virtualization";
    case 21:
        return "control protection";
    default:
        return "exception";
    }
}

void log_hex64(const char* name, u64 value) noexcept
{
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    const char* hex = "0123456789abcdef";
    for (int i = 0; i < 16; ++i)
    {
        const u32 shift = static_cast<u32>((15 - i) * 4);
        buf[2 + i] = hex[(value >> shift) & 0xFULL];
    }
    buf[18] = '\0';
    log::write(log::Level::Error, "reg", "%s %s", name, buf);
}

void dump_frame(const InterruptFrame* f) noexcept
{
    log_hex64("rax ", f->rax);
    log_hex64("rbx ", f->rbx);
    log_hex64("rcx ", f->rcx);
    log_hex64("rdx ", f->rdx);
    log_hex64("rsi ", f->rsi);
    log_hex64("rdi ", f->rdi);
    log_hex64("rbp ", f->rbp);
    log_hex64("r8  ", f->r8);
    log_hex64("r9  ", f->r9);
    log_hex64("r10 ", f->r10);
    log_hex64("r11 ", f->r11);
    log_hex64("r12 ", f->r12);
    log_hex64("r13 ", f->r13);
    log_hex64("r14 ", f->r14);
    log_hex64("r15 ", f->r15);
    log_hex64("rip ", f->rip);
    log_hex64("cs  ", f->cs);
    log_hex64("rflg", f->rflags);
    log_hex64("rsp ", f->rsp);
    log_hex64("ss  ", f->ss);
    log_hex64("err ", f->error_code);
}

// Called when an exception originates in ring 3. Logs the fault and
// terminates the offending task instead of panicking the kernel.
//
// The CPL check is on the CS selector saved in the interrupt frame:
// if the low 2 bits are 3, the fault happened in user mode.
void handle_user_exception(InterruptFrame* f) noexcept
{
    const u64 v = f->vector;
    const u64 cr2 = (v == 14) ? []() -> u64
    {
        u64 x = 0;
        asm volatile("mov %%cr2, %0" : "=r"(x));
        return x;
    }()
        : 0;

    auto* cur = sched::scheduler_current();
    const char* tname = cur ? cur->name : "?";
    const u64 tid = cur ? static_cast<u64>(cur->tid) : 0;

    log::write(log::Level::Error, "exc", "user-mode %s in tid=%llu '%s': rip=0x%llx err=0x%llx",
               exception_name(v), tid, tname, static_cast<unsigned long long>(f->rip),
               static_cast<unsigned long long>(f->error_code));

    if (v == 14)
        log::write(log::Level::Error, "exc", "fault address = 0x%llx",
                   static_cast<unsigned long long>(cr2));

    // If this is the init task, killing it would idle the system. Log
    // and halt the task's execution but keep the kernel alive by
    // dropping it back to the scheduler.
    log::write(log::Level::Warn, "exc", "terminating user task; kernel remains alive");

    sched::scheduler_exit_current(static_cast<int>(v) + 128);
    // scheduler_exit_current is [[noreturn]].
}

// Dispatch CPU exceptions according to their privilege level. User-mode
// faults are isolated to the current task; kernel-mode faults are fatal.
void handle_exception(InterruptFrame* f) noexcept
{
    if ((f->cs & 0x3ULL) == 0x3ULL)
    {
        handle_user_exception(f);
        return;
    }

    log::write(log::Level::Error, "exc", "kernel-mode %s: rip=0x%llx err=0x%llx",
               exception_name(f->vector), static_cast<unsigned long long>(f->rip),
               static_cast<unsigned long long>(f->error_code));
    dump_frame(f);
    panic("fatal kernel exception");
}

void handle_irq(u8 irq, InterruptFrame* /*f*/) noexcept
{
    if (irq == 0)
    {
        pit_on_tick();
        notyvos_e1000_poll();
        sched::scheduler_tick();
        // Mark the clock dirty. The actual repaint happens in the idle loop.
        static u32 tick_div = 0;
        if (++tick_div >= 25)
        { // once per 250 ms
            tick_div = 0;
            gfx::Compositor::update_clock();
        }
    }
    else if (irq == 1)
    {
        keyboard_irq_handler();
    }
    else if (irq == 12)
    {
        mouse_irq_handler();
    }
}

} // namespace

extern "C" void notyvos_isr_dispatch(InterruptFrame* frame) noexcept
{
    const u64 vec = frame->vector;
    if (vec < 32)
    {
        handle_exception(frame);
    }
    else if (vec < 48)
    {
        const u8 irq = static_cast<u8>(vec - 32);
        handle_irq(irq, frame);
        pic_send_eoi(irq);
    }
    else
    {
        log::write(log::Level::Warn, "isr", "unhandled vector %llu",
                   static_cast<unsigned long long>(vec));
    }
}

} // namespace notyvos::arch::x86_64
