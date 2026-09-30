#include <kernel/arch/x86_64/io.hpp>
#include <kernel/arch/x86_64/rtc.hpp>
#include <kernel/log.hpp>

namespace notyvos::arch::x86_64::rtc
{

namespace
{
constexpr u16 kCmosAddr = 0x70;
constexpr u16 kCmosData = 0x71;

constexpr u8 kRegSeconds = 0x00;
constexpr u8 kRegMinutes = 0x02;
constexpr u8 kRegHours   = 0x04;
constexpr u8 kRegDay     = 0x07;
constexpr u8 kRegMonth   = 0x08;
constexpr u8 kRegYear    = 0x09;
constexpr u8 kRegStatusA = 0x0A;
constexpr u8 kRegStatusB = 0x0B;
constexpr u8 kRegCentury = 0x32;

constexpr u8 kStatusAUpd = 0x80;
constexpr u8 kStatusBBin = 0x04;
constexpr u8 kStatusB24h = 0x02;

u8 cmos_read(u8 reg) noexcept
{
    // Bit 7 of the register select gates NMI. Keep it set to disable NMI
    // while we access CMOS - the alternative is spurious NMIs on some
    // chipsets when reading the RTC.
    outb(kCmosAddr, static_cast<u8>(reg | 0x80u));
    return inb(kCmosData);
}

bool update_in_progress() noexcept
{
    return (cmos_read(kRegStatusA) & kStatusAUpd) != 0;
}

u8 bcd_to_bin(u8 bcd) noexcept
{
    return static_cast<u8>(((bcd >> 4) & 0x0Fu) * 10u + (bcd & 0x0Fu));
}

} // namespace

DateTime read() noexcept
{
    // Wait for the RTC to finish its internal update (takes ~2 ms).
    u32 guard = 0;
    while (update_in_progress() && guard++ < 1000000u)
    {
    }

    DateTime dt{};
    const u8 status_b = cmos_read(kRegStatusB);
    const bool binary = (status_b & kStatusBBin) != 0;
    const bool hour24 = (status_b & kStatusB24h) != 0;

    u8 sec = cmos_read(kRegSeconds);
    u8 mn  = cmos_read(kRegMinutes);
    u8 hr  = cmos_read(kRegHours);
    u8 dy  = cmos_read(kRegDay);
    u8 mo  = cmos_read(kRegMonth);
    u8 yr  = cmos_read(kRegYear);
    u8 cen = cmos_read(kRegCentury);

    if (!binary)
    {
        sec = bcd_to_bin(sec);
        mn  = bcd_to_bin(mn);
        const u8 pm = static_cast<u8>(hr & 0x80u);
        hr &= 0x7Fu;
        hr  = bcd_to_bin(hr);
        dy  = bcd_to_bin(dy);
        mo  = bcd_to_bin(mo);
        yr  = bcd_to_bin(yr);
        cen = bcd_to_bin(cen);

        if (!hour24)
        {
            const bool is_pm = pm != 0;
            if (hr == 12u)
                hr = is_pm ? 12u : 0u;
            else if (is_pm)
                hr = static_cast<u8>(hr + 12u);
        }
    }

    dt.year   = (cen == 0u || cen > 99u) ? static_cast<u16>(2000u + yr)
                                         : static_cast<u16>(cen * 100u + yr);
    dt.month  = mo;
    dt.day    = dy;
    dt.hour   = hr;
    dt.minute = mn;
    dt.second = sec;
    return dt;
}

namespace
{
// Howard Hinnant's days_from_civil algorithm, days since 1970-01-01.
i64 days_from_civil(i64 y, i64 m, i64 d) noexcept
{
    y -= (m <= 2) ? 1 : 0;
    const i64 era = (y >= 0 ? y : y - 399) / 400;
    const u32 yoe = static_cast<u32>(y - era * 400);
    const u32 mp  = (m > 2) ? static_cast<u32>(m - 3) : static_cast<u32>(m + 9);
    const u32 doy = (153u * mp + 2u) / 5u + static_cast<u32>(d) - 1u;
    const u32 doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<i64>(doe) - 719468;
}
} // namespace

u64 to_unix_seconds(const DateTime& dt) noexcept
{
    const i64 days = days_from_civil(static_cast<i64>(dt.year),
                                     static_cast<i64>(dt.month),
                                     static_cast<i64>(dt.day));
    const u64 secs = static_cast<u64>(days) * 86400u
                   + static_cast<u64>(dt.hour) * 3600u
                   + static_cast<u64>(dt.minute) * 60u
                   + static_cast<u64>(dt.second);
    return secs;
}

void init() noexcept
{
    const DateTime dt = read();
    log::write(log::Level::Info, "rtc", "%04llu-%02llu-%02llu %02llu:%02llu:%02llu",
               static_cast<unsigned long long>(dt.year),
               static_cast<unsigned long long>(dt.month),
               static_cast<unsigned long long>(dt.day),
               static_cast<unsigned long long>(dt.hour),
               static_cast<unsigned long long>(dt.minute),
               static_cast<unsigned long long>(dt.second));
}

} // namespace notyvos::arch::x86_64::rtc