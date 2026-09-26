#include <kernel/arch/x86_64/isr.hpp>
#include <kernel/arch/x86_64/pic.hpp>
#include <kernel/arch/x86_64/pit.hpp>
#include <kernel/log.hpp>
#include <kernel/panic.hpp>
#include <kernel/sched/scheduler.hpp>

namespace notyvos::arch::x86_64
{

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

[[noreturn]] void handle_exception(InterruptFrame* f) noexcept
{
    const u64 v = f->vector;
    log::write(log::Level::Error, "exc", "vector %llu (%s) err=0x%llx rip=0x%llx",
               static_cast<unsigned long long>(v), exception_name(v),
               static_cast<unsigned long long>(f->error_code),
               static_cast<unsigned long long>(f->rip));
    if (v == 14)
    {
        u64 cr2 = 0;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        log::write(log::Level::Error, "exc", "fault address = 0x%llx",
                   static_cast<unsigned long long>(cr2));
    }
    dump_frame(f);
    panic("unhandled exception");
}

void handle_irq(u8 irq, InterruptFrame* /*f*/) noexcept
{
    if (irq == 0)
    {
        pit_on_tick();
        sched::scheduler_tick();
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
