#include <kernel/fs/nyfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::fs
{

namespace
{
constexpr u32 kBlockSize = kNyfsBlockSize;
constexpr u32 kInodeSize = kNyfsInodeSize;
constexpr u32 kInodeCount = kNyfsInodeCount;
constexpr u32 kInodePerBlock = kBlockSize / kInodeSize;
constexpr u32 kDirectBlocks = kNyfsDirectBlocks;

block::BlockDevice* f_dev = nullptr;
u32 f_inode_table_lba = 0;
u32 f_data_start = 0;
u32 f_data_blocks = 0;

bool f_read_block(u32 lba, void* buf) noexcept
{
    return block::block_read(f_dev, lba, 1, buf) == static_cast<isize>(kBlockSize);
}

bool f_write_block(u32 lba, const void* buf) noexcept
{
    return block::block_write(f_dev, lba, 1, buf) == static_cast<isize>(kBlockSize);
}

bool f_read_inode(u32 idx, NyfsInode* out) noexcept
{
    if (idx >= kInodeCount)
        return false;
    const u32 lba = f_inode_table_lba + idx / kInodePerBlock;
    const u32 off = (idx % kInodePerBlock) * kInodeSize;
    u8 buf[kBlockSize];
    if (!f_read_block(lba, buf))
        return false;
    libk::memcpy(out, buf + off, kInodeSize);
    return true;
}

} // namespace

bool nyfs_fsck(bool repair) noexcept
{
    f_dev = nyfs_device();
    if (!f_dev)
        return false;

    u8 sb_buf[kBlockSize];
    if (!f_read_block(0, sb_buf))
        return false;
    NyfsSuperblock sb{};
    libk::memcpy(&sb, sb_buf, sizeof(sb));
    f_inode_table_lba = sb.inode_table_lba;
    f_data_start = sb.data_start_lba;
    f_data_blocks = sb.data_blocks;

    const u32 bm_bytes = f_data_blocks / 8u + (f_data_blocks % 8u ? 1u : 0u);
    auto* used = static_cast<u8*>(mm::Heap::allocate(bm_bytes ? bm_bytes : 1));
    if (!used)
        return false;
    libk::memset(used, 0, bm_bytes);

    u32 errors = 0;
    u32 inodes_scanned = 0;
    u32 blocks_referenced = 0;

    for (u32 i = 0; i < kInodeCount; ++i)
    {
        NyfsInode in{};
        if (!f_read_inode(i, &in))
            continue;
        if (in.mode == 0)
            continue;
        ++inodes_scanned;

        for (u32 b = 0; b < kDirectBlocks; ++b)
        {
            const u32 lba = in.blocks[b];
            if (lba == 0)
                continue;
            if (lba < f_data_start || lba >= f_data_start + f_data_blocks)
            {
                ++errors;
                if (repair)
                {
                    in.blocks[b] = 0;
                }
                continue;
            }
            const u32 idx = lba - f_data_start;
            used[idx / 8] = static_cast<u8>(used[idx / 8] | (1u << (idx % 8)));
            ++blocks_referenced;
        }

        if (in.indirect)
        {
            if (in.indirect < f_data_start || in.indirect >= f_data_start + f_data_blocks)
            {
                ++errors;
                if (repair)
                    in.indirect = 0;
            }
            else
            {
                const u32 idx = in.indirect - f_data_start;
                used[idx / 8] = static_cast<u8>(used[idx / 8] | (1u << (idx % 8)));
                ++blocks_referenced;

                u8 ind_buf[kBlockSize];
                if (f_read_block(in.indirect, ind_buf))
                {
                    const u32 per_block = kBlockSize / 4u;
                    for (u32 k = 0; k < per_block; ++k)
                    {
                        u32 lba = 0;
                        libk::memcpy(&lba, ind_buf + k * 4u, 4);
                        if (lba == 0)
                            continue;
                        if (lba < f_data_start || lba >= f_data_start + f_data_blocks)
                        {
                            ++errors;
                            if (repair)
                            {
                                const u32 zero = 0;
                                libk::memcpy(ind_buf + k * 4u, &zero, 4);
                            }
                            continue;
                        }
                        const u32 idx2 = lba - f_data_start;
                        used[idx2 / 8] = static_cast<u8>(used[idx2 / 8] | (1u << (idx2 % 8)));
                        ++blocks_referenced;
                    }
                    if (repair)
                        (void)f_write_block(in.indirect, ind_buf);
                }
            }
        }

        if (repair)
        {
            const u32 lba = f_inode_table_lba + i / kInodePerBlock;
            const u32 off = (i % kInodePerBlock) * kInodeSize;
            u8 buf[kBlockSize];
            if (f_read_block(lba, buf))
            {
                libk::memcpy(buf + off, &in, kInodeSize);
                (void)f_write_block(lba, buf);
            }
        }
    }

    // Compare the derived bitmap against the on-disk block bitmap.
    for (u32 i = 0; i < sb.block_bitmap_blocks; ++i)
    {
        u8 buf[kBlockSize];
        if (!f_read_block(sb.block_bitmap_lba + i, buf))
            break;
        const u32 start = i * kBlockSize;
        if (start >= bm_bytes)
            break;
        const u32 take = (bm_bytes - start < kBlockSize) ? (bm_bytes - start) : kBlockSize;
        for (u32 k = 0; k < take; ++k)
        {
            if (buf[k] != used[start + k])
            {
                ++errors;
                if (repair)
                    buf[k] = used[start + k];
            }
        }
        if (repair)
            (void)f_write_block(sb.block_bitmap_lba + i, buf);
    }

    log::write(errors == 0 ? log::Level::Info : log::Level::Warn, "nyfs-fsck",
               "scanned %u inodes, %u blocks referenced, %u errors%s",
               inodes_scanned, blocks_referenced, errors,
               repair ? " (repaired)" : "");

    mm::Heap::deallocate(used);
    return errors == 0;
}

} // namespace notyvos::fs
