#include <kernel/arch/x86_64/serial.hpp>

namespace notyvos::arch::x86_64 {

u16 SerialPort::s_port = SerialPort::kCom1;

namespace {
inline void outb(u16 port, u8 value) noexcept {
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port) : "memory");
}
inline u8 inb(u16 port) noexcept {
    u8 v;
    asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}
} // namespace

void SerialPort::init(u16 port) noexcept {
    s_port = port;
    outb(port + 1, 0x00); // disable interrupts
    outb(port + 3, 0x80); // enable DLAB
    outb(port + 0, 0x03); // divisor low: 3 -> 38400 baud... use 1 for 115200
    outb(port + 0, 0x01); // divisor low: 1 -> 115200
    outb(port + 1, 0x00); // divisor high: 0
    outb(port + 3, 0x03); // 8 bits, no parity, one stop bit
    outb(port + 2, 0xC7); // enable FIFO, clear, 14-byte threshold
    outb(port + 4, 0x0B); // IRQs enabled, RTS/DSR set
    (void)inb(port + 5);
}

bool SerialPort::is_ready() noexcept {
    return (inb(s_port + 5) & 0x20) != 0;
}

void SerialPort::wait_tx_empty() noexcept {
    while (!is_ready()) {
        asm volatile("pause");
    }
}

void SerialPort::write_char(char c) noexcept {
    if (c == '\n') {
        wait_tx_empty();
        outb(s_port, '\r');
    }
    wait_tx_empty();
    outb(s_port, static_cast<u8>(c));
}

void SerialPort::write(const char* s) noexcept {
    if (!s) return;
    while (*s) write_char(*s++);
}

void SerialPort::write(const char* s, usize n) noexcept {
    if (!s) return;
    for (usize i = 0; i < n; ++i) write_char(s[i]);
}

} // namespace notyvos::arch::x86_64