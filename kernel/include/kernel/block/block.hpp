#pragma once
#include <kernel/types.hpp>

namespace notyvos::block
{

constexpr u32 kSectorSize = 512;
constexpr u32 kMaxDevices = 8;

struct BlockDevice
{
    char name[16];
    u64 sector_count;
    u32 logical_sector_size; // usually 512
    void* driver_data;
    isize (*read_sectors)(BlockDevice* dev, u64 lba, u32 count, void* buf);
    isize (*write_sectors)(BlockDevice* dev, u64 lba, u32 count, const void* buf);
    bool read_only;
};

void block_init() noexcept;
int block_register(BlockDevice* dev) noexcept; // returns index or -1
BlockDevice* block_get(u32 index) noexcept;
u32 block_count() noexcept;

// Convenience wrappers.
isize block_read(BlockDevice* dev, u64 lba, u32 count, void* buf) noexcept;
isize block_write(BlockDevice* dev, u64 lba, u32 count, const void* buf) noexcept;

} // namespace notyvos::block
