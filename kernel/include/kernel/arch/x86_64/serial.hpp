#pragma once

#include <kernel/types.hpp>

namespace notyvos::arch::x86_64 {

class SerialPort {
public:
    static constexpr u16 kCom1 = 0x3F8;

    static void init(u16 port = kCom1) noexcept;
    static void write_char(char c) noexcept;
    static void write(const char* s) noexcept;
    static void write(const char* s, usize n) noexcept;
    static bool is_ready() noexcept;

private:
    static u16 s_port;
    static void wait_tx_empty() noexcept;
};

} // namespace notyvos::arch::x86_64