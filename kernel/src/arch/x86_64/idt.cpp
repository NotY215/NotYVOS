#include <kernel/arch/x86_64/gdt.hpp>
#include <kernel/arch/x86_64/idt.hpp>
#include <kernel/arch/x86_64/isr.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64
{

extern "C" void notyvos_idt_flush(const IdtPointer* ptr) noexcept;

namespace
{
alignas(16) IdtEntry g_idt[256] = {};
alignas(16) IdtPointer g_idt_ptr = {};
} // namespace

void idt_set_gate(u8 vector, uptr handler, u16 selector, u8 ist, u8 type_attr) noexcept
{
    g_idt[vector].offset_low = static_cast<u16>(handler & 0xFFFF);
    g_idt[vector].selector = selector;
    g_idt[vector].ist = static_cast<u8>(ist & 0x7);
    g_idt[vector].type_attr = type_attr;
    g_idt[vector].offset_mid = static_cast<u16>((handler >> 16) & 0xFFFF);
    g_idt[vector].offset_high = static_cast<u32>((handler >> 32) & 0xFFFFFFFFULL);
    g_idt[vector].reserved = 0;
}

void idt_init() noexcept
{
    constexpr u8 kInterruptGateDpl0 = 0x8E; // P=1, DPL=0, type=0xE (64-bit int gate)
    constexpr u8 kInterruptGateDpl3 = 0xEE; // P=1, DPL=3

    // Default: unhandled for all vectors
    for (u32 i = 0; i < 256; ++i)
    {
        idt_set_gate(static_cast<u8>(i), reinterpret_cast<uptr>(&isr_unhandled),
                     gdt_sel::kKernelCode, 0, kInterruptGateDpl0);
    }

    // Exceptions 0..31
    for (u32 i = 0; i < 32; ++i)
    {
        u8 attr = kInterruptGateDpl0;
        if (i == 3 || i == 4)
            attr = kInterruptGateDpl3; // int3, into
        idt_set_gate(static_cast<u8>(i), isr_stub_table[i], gdt_sel::kKernelCode, 0, attr);
    }

    // PIC IRQs 32..47
    for (u32 i = 32; i < 48; ++i)
    {
        idt_set_gate(static_cast<u8>(i), isr_stub_table[i], gdt_sel::kKernelCode, 0,
                     kInterruptGateDpl0);
    }

    // IST overrides
    idt_set_gate(2, isr_stub_table[2], gdt_sel::kKernelCode, 2, kInterruptGateDpl0); // NMI -> IST2
    idt_set_gate(8, isr_stub_table[8], gdt_sel::kKernelCode, 1, kInterruptGateDpl0); // #DF -> IST1

    g_idt_ptr.limit = sizeof(IdtEntry) * 256 - 1;
    g_idt_ptr.base = reinterpret_cast<u64>(&g_idt[0]);

    notyvos_idt_flush(&g_idt_ptr);

    log::write(log::Level::Info, "idt", "installed 256 vectors");
}

} // namespace notyvos::arch::x86_64
