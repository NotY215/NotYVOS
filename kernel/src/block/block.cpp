#include <kernel/block/block.hpp>
#include <kernel/log.hpp>

namespace notyvos::block
{

namespace
{
BlockDevice* g_devices[kMaxDevices] = {};
u32 g_count = 0;
} // namespace

void block_init() noexcept
{
    for (u32 i = 0; i < kMaxDevices; ++i)
        g_devices[i] = nullptr;
    g_count = 0;
    log::write(log::Level::Info, "blk", "block layer initialized");
}

int block_register(BlockDevice* dev) noexcept
{
    if (!dev)
        return -1;
    for (u32 i = 0; i < kMaxDevices; ++i)
    {
        if (!g_devices[i])
        {
            g_devices[i] = dev;
            if (i + 1 > g_count)
                g_count = i + 1;
            log::write(log::Level::Info, "blk", "registered %s: %llu sectors, %s", dev->name,
                       static_cast<unsigned long long>(dev->sector_count),
                       dev->read_only ? "ro" : "rw");
            return static_cast<int>(i);
        }
    }
    return -1;
}

BlockDevice* block_get(u32 index) noexcept
{
    return (index < kMaxDevices) ? g_devices[index] : nullptr;
}

u32 block_count() noexcept
{
    return g_count;
}

isize block_read(BlockDevice* dev, u64 lba, u32 count, void* buf) noexcept
{
    if (!dev || !dev->read_sectors)
        return -1;
    if (lba + count > dev->sector_count)
        return -1;
    return dev->read_sectors(dev, lba, count, buf);
}

isize block_write(BlockDevice* dev, u64 lba, u32 count, const void* buf) noexcept
{
    if (!dev || !dev->write_sectors)
        return -1;
    if (dev->read_only)
        return -1;
    if (lba + count > dev->sector_count)
        return -1;
    return dev->write_sectors(dev, lba, count, buf);
}

} // namespace notyvos::block
