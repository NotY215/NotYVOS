#include <kernel/acpi/acpi.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/paging.hpp>
#include <kernel/mm/vmm.hpp>

namespace notyvos::acpi
{

namespace
{

struct Rsdp
{
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 ext_checksum;
    u8 reserved[3];
} __attribute__((packed));

struct SdtHeader
{
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} __attribute__((packed));

struct GenericAddress
{
    u8 address_space;
    u8 bit_width;
    u8 bit_offset;
    u8 access_size;
    u64 address;
} __attribute__((packed));

struct Fadt
{
    SdtHeader h;
    u32 firmware_ctrl;
    u32 dsdt;
    u8 reserved0;
    u8 preferred_pm_profile;
    u16 sci_int;
    u32 smi_cmd;
    u8 acpi_enable;
    u8 acpi_disable;
    u8 s4bios_req;
    u8 pstate_cnt;
    u32 pm1a_evt_blk;
    u32 pm1b_evt_blk;
    u32 pm1a_cnt_blk;
    u32 pm1b_cnt_blk;
    u32 pm2_cnt_blk;
    u32 pm_tmr_blk;
    u32 gpe0_blk;
    u32 gpe1_blk;
    u8 pm1_evt_len;
    u8 pm1_cnt_len;
    u8 pm2_cnt_len;
    u8 pm_tmr_len;
    u8 gpe0_blk_len;
    u8 gpe1_blk_len;
    u8 gpe1_base;
    u8 cst_cnt;
    u16 p_lvl2_lat;
    u16 p_lvl3_lat;
    u16 flush_size;
    u16 flush_stride;
    u8 duty_offset;
    u8 duty_width;
    u8 day_alrm;
    u8 mon_alrm;
    u8 century;
    u16 iapc_boot_arch;
    u8 reserved1;
    u32 flags;
    GenericAddress reset_reg;
    u8 reset_value;
    u8 reserved2[3];
} __attribute__((packed));

SdtHeader* g_fadt = nullptr;
u64 g_hhdm = 0;
u16 g_pm1a_cnt = 0;
GenericAddress g_reset_reg = {};
bool g_has_reset_reg = false;
bool g_power_ok = false;

u8 checksum8(const u8* p, u32 len) noexcept
{
    u8 s = 0;
    for (u32 i = 0; i < len; ++i)
        s = static_cast<u8>(s + p[i]);
    return s;
}

// Map [phys, phys+size) into the kernel VA space at phys+hhdm, then return
// the corresponding virtual pointer. Safe to call on already-mapped pages:
// VirtualMemory::map_page overwrites the PTE with the same physical address.
const void* map_phys(u64 phys, u32 size) noexcept
{
    if (!g_hhdm)
        return nullptr;
    const u64 start = phys & ~0xFFFULL;
    const u64 end = (phys + size + 0xFFFULL) & ~0xFFFULL;
    for (u64 p = start; p < end; p += 0x1000)
    {
        mm::VirtualMemory::map_page(static_cast<uptr>(p + g_hhdm), static_cast<uptr>(p),
                                    mm::page_flags::Present | mm::page_flags::Writable);
    }
    return reinterpret_cast<const void*>(phys + g_hhdm);
}

} // namespace

bool init(u64 hhdm_offset, void* limine_rsdp_addr) noexcept
{
    g_hhdm = hhdm_offset;

    if (!limine_rsdp_addr)
    {
        log::write(log::Level::Warn, "acpi", "no RSDP from Limine");
        return false;
    }

    // Limine returns either a physical address or an HHDM-mapped pointer
    // depending on the version. If the top bits are set, treat as virtual.
    // Otherwise, treat as physical and map it ourselves.
    const uptr raw = reinterpret_cast<uptr>(limine_rsdp_addr);
    const void* rsdp_virt;
    if (raw >= 0xFFFF800000000000ULL)
    {
        rsdp_virt = limine_rsdp_addr;
    }
    else
    {
        rsdp_virt = map_phys(static_cast<u64>(raw), sizeof(Rsdp));
        if (!rsdp_virt)
            return false;
    }

    const auto* rsdp = static_cast<const Rsdp*>(rsdp_virt);
    if (libk::memcmp(rsdp->signature, "RSD PTR ", 8) != 0)
    {
        log::write(log::Level::Warn, "acpi", "RSDP signature wrong");
        return false;
    }
    if (checksum8(reinterpret_cast<const u8*>(rsdp), 20) != 0)
    {
        log::write(log::Level::Warn, "acpi", "RSDP checksum bad");
        return false;
    }

    log::write(log::Level::Info, "acpi", "RSDP at 0x%llx, revision %llu",
               static_cast<unsigned long long>(raw),
               static_cast<unsigned long long>(rsdp->revision));

    const u64 rsdt_phys = rsdp->rsdt_address;
    if (!rsdt_phys)
    {
        log::write(log::Level::Warn, "acpi", "RSDT physical is 0");
        return false;
    }

    // First read the RSDT header to learn its length.
    const auto* rsdt_hdr = static_cast<const SdtHeader*>(map_phys(rsdt_phys, sizeof(SdtHeader)));
    if (!rsdt_hdr)
        return false;
    if (libk::memcmp(rsdt_hdr->signature, "RSDT", 4) != 0)
    {
        log::write(log::Level::Warn, "acpi", "RSDT signature wrong");
        return false;
    }

    const u32 rsdt_len = rsdt_hdr->length;
    if (rsdt_len < sizeof(SdtHeader) || rsdt_len > 0x10000u)
    {
        log::write(log::Level::Warn, "acpi", "RSDT length invalid");
        return false;
    }

    const auto* rsdt = static_cast<const SdtHeader*>(map_phys(rsdt_phys, rsdt_len));
    if (!rsdt)
        return false;

    if (checksum8(reinterpret_cast<const u8*>(rsdt), rsdt_len) != 0)
    {
        log::write(log::Level::Warn, "acpi", "RSDT checksum bad");
        return false;
    }

    const u32 entries = (rsdt_len - sizeof(SdtHeader)) / 4;
    const auto* table_ptrs =
        reinterpret_cast<const u32*>(reinterpret_cast<const u8*>(rsdt) + sizeof(SdtHeader));

    for (u32 i = 0; i < entries; ++i)
    {
        const uptr tbl_phys = table_ptrs[i];
        if (!tbl_phys)
            continue;
        const auto* tbl =
            static_cast<const SdtHeader*>(map_phys(static_cast<u64>(tbl_phys), sizeof(SdtHeader)));
        if (!tbl)
            continue;
        if (libk::memcmp(tbl->signature, "FACP", 4) == 0)
        {
            g_fadt = const_cast<SdtHeader*>(tbl);
            break;
        }
    }

    if (!g_fadt)
    {
        log::write(log::Level::Warn, "acpi", "FADT not found");
        return false;
    }

    const auto* fadt = reinterpret_cast<const Fadt*>(g_fadt);
    g_pm1a_cnt = static_cast<u16>(fadt->pm1a_cnt_blk);
    g_reset_reg = fadt->reset_reg;
    g_has_reset_reg = (g_reset_reg.address != 0);

    log::write(log::Level::Info, "acpi", "FADT: pm1a_cnt=0x%llx, reset_reg=0x%llx",
               static_cast<unsigned long long>(g_pm1a_cnt),
               static_cast<unsigned long long>(g_reset_reg.address));

    g_power_ok = (g_pm1a_cnt != 0);
    return true;
}

bool power_available() noexcept
{
    return g_power_ok;
}

void power_off() noexcept
{
    log::write(log::Level::Info, "acpi", "power off (S5)");

    if (g_power_ok)
    {
        const u16 slp_en = 1u << 13;
        for (u16 t = 0; t < 8; ++t)
        {
            const u16 v = static_cast<u16>((t << 10) | slp_en);
            asm volatile("outw %0, %1" ::"a"(v), "Nd"(g_pm1a_cnt));
        }
    }

    asm volatile("outb %0, %1" ::"a"(static_cast<u8>(0xFE)), "Nd"(static_cast<u16>(0x64)));
    for (;;)
        asm volatile("hlt");
}

void restart() noexcept
{
    log::write(log::Level::Info, "acpi", "restart");

    if (g_has_reset_reg && g_reset_reg.address != 0)
    {
        const u8 v = 0x06;
        if (g_reset_reg.address_space == 1)
        {
            asm volatile("outb %0, %1" ::"a"(v), "Nd"(static_cast<u16>(g_reset_reg.address)));
        }
    }

    asm volatile("outb %0, %1" ::"a"(static_cast<u8>(0xFE)), "Nd"(static_cast<u16>(0x64)));
    for (;;)
        asm volatile("hlt");
}

} // namespace notyvos::acpi
