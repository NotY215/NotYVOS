#pragma once
#include <kernel/types.hpp>

namespace notyvos::arch::x86_64::rtc
{

struct DateTime
{
    u16 year;  // full year, e.g. 2026
    u8 month;  // 1..12
    u8 day;    // 1..31
    u8 hour;   // 0..23
    u8 minute; // 0..59
    u8 second; // 0..59
};

void init() noexcept;                             // reads RTC once, logs it
DateTime read() noexcept;                         // re-reads RTC
u64 to_unix_seconds(const DateTime& dt) noexcept; // days-since-1970 based

} // namespace notyvos::arch::x86_64::rtc
