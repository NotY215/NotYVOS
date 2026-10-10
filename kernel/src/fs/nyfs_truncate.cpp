#include <kernel/fs/nyfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>

namespace notyvos::fs
{

namespace
{

constexpr u32 kBlockSize = kNyfsBlockSize;
constexpr u32 kInodeSize = kNyfsInodeSize;
constexpr u32 kInodeCount = kNyfsInodeCount;
constexpr u32 kInodePerBlock = kBlockSize / kInodeSize;
constexpr u32 kDirectBlocks = kNyfsDirectBlocks;

struct TruncPriv
{
    u32 inode;
};

bool t_read_block(u32 lba, void* buf) noexcept
{
    return block::block_read(nyfs_device(), lba, 1, buf) ==
           static_cast<isize>(kBlockSize);
}

bool t_write_block(u32 lba, const void* buf) noexcept
{
    return block::block_write(nyfs_device(), lba, 1, buf) ==
           static_cast<isize>(kBlockSize);
}

bool t_read_inode(u32 idx, NyfsInode* out) noexcept
{
    if (idx >= kInodeCount)
        return false;
    u8 sb_buf[kBlockSize];
    if (!t_read_block(0, sb_buf))
        return false;
    NyfsSuperblock sb{};
    libk::memcpy(&sb, sb_buf, sizeof(sb));

    const u32 lba = sb.inode_table_lba + idx / kInodePerBlock;
    const u32 off = (idx % kInodePerBlock) * kInodeSize;
    u8 buf[kBlockSize];
    if (!t_read_block(lba, buf))
        return false;
    libk::memcpy(out, buf + off, kInodeSize);
    return true;
}

bool t_write_inode(u32 idx, const NyfsInode* in) noexcept
{
    if (idx >= kInodeCount)
        return false;
    u8 sb_buf[kBlockSize];
    if (!t_read_block(0, sb_buf))
        return false;
    NyfsSuperblock sb{};
    libk::memcpy(&sb, sb_buf, sizeof(sb));

    const u32 lba = sb.inode_table_lba + idx / kInodePerBlock;
    const u32 off = (idx % kInodePerBlock) * kInodeSize;
    u8 buf[kBlockSize];
    if (!t_read_block(lba, buf))
        return false;
    libk::memcpy(buf + off, in, kInodeSize);
    return t_write_block(lba, buf);
}

void t_free_bitmap_bit(u32 lba_index) noexcept
{
    u8 sb_buf[kBlockSize];
    if (!t_read_block(0, sb_buf))
        return;
    NyfsSuperblock sb{};
    libk::memcpy(&sb, sb_buf, sizeof(sb));

    const u32 per_block = kBlockSize * 8u;
    const u32 bm_block = lba_index / per_block;
    const u32 bm_bit = lba_index % per_block;
    const u32 bm_lba = sb.block_bitmap_lba + bm_block;
    if (bm_block >= sb.block_bitmap_blocks)
        return;

    u8 buf[kBlockSize];
    if (!t_read_block(bm_lba, buf))
        return;
    buf[bm_bit / 8] = static_cast<u8>(buf[bm_bit / 8] & ~(1u << (bm_bit % 8)));
    (void)t_write_block(bm_lba, buf);
}

} // namespace

int nyfs_truncate(VNode* node, u32 new_size)
{
    if (!node || !node->priv)
        return -1;
    const auto* p = static_cast<const TruncPriv*>(node->priv);
    NyfsInode in{};
    if (!t_read_inode(p->inode, &in))
        return -1;

    const u32 old_size = in.size;
    if (new_size == old_size)
        return 0;

    if (new_size > old_size)
    {
        // Grow: zero-fill the new region. Blocks are allocated by
        // nyfs_write_range; here we simply extend the recorded size.
        // Reading beyond the last data block returns zeros.
        in.size = new_size;
        (void)t_write_inode(p->inode, &in);
        return 0;
    }

    // Shrink. Free trailing blocks. Keep whole blocks that are still
    // partially in use; only release blocks entirely past new_size.
    const u32 last_used_block = (new_size == 0) ? 0 : ((new_size + kBlockSize - 1) / kBlockSize);

    // Direct blocks.
    for (u32 b = last_used_block; b < kDirectBlocks; ++b)
    {
        const u32 lba = in.blocks[b];
        if (lba == 0)
            continue;
        if (lba >= 1 && lba < (1u << 31))
            t_free_bitmap_bit(lba);
        in.blocks[b] = 0;
    }

    // Indirect block.
    const u32 direct_capacity = kDirectBlocks * kBlockSize;
    if (new_size <= direct_capacity && in.indirect != 0)
    {
        u8 ind_buf[kBlockSize];
        if (t_read_block(in.indirect, ind_buf))
        {
            const u32 per_block = kBlockSize / 4u;
            for (u32 k = 0; k < per_block; ++k)
            {
                u32 lba = 0;
                libk::memcpy(&lba, ind_buf + k * 4u, 4);
                if (lba != 0)
                    t_free_bitmap_bit(lba);
            }
        }
        t_free_bitmap_bit(in.indirect);
        in.indirect = 0;
    }
    else if (in.indirect != 0)
    {
        // Partial truncation of indirect. Free trailing entries.
        u8 ind_buf[kBlockSize];
        if (t_read_block(in.indirect, ind_buf))
        {
            const u32 per_block = kBlockSize / 4u;
            const u32 first_to_free = last_used_block > kDirectBlocks
                                          ? (last_used_block - kDirectBlocks)
                                          : 0u;
            for (u32 k = first_to_free; k < per_block; ++k)
            {
                u32 lba = 0;
                libk::memcpy(&lba, ind_buf + k * 4u, 4);
                if (lba == 0)
                    continue;
                t_free_bitmap_bit(lba);
                const u32 zero = 0;
                libk::memcpy(ind_buf + k * 4u, &zero, 4);
            }
            (void)t_write_block(in.indirect, ind_buf);
        }
    }

    in.size = new_size;
    (void)t_write_inode(p->inode, &in);
    return 0;
}

int nyfs_chmod(VNode* node, u16 mode)
{
    if (!node || !node->priv)
        return -1;
    const auto* p = static_cast<const TruncPriv*>(node->priv);
    NyfsInode in{};
    if (!t_read_inode(p->inode, &in))
        return -1;
    in.perms = static_cast<u16>(mode & kPermAll);
    return t_write_inode(p->inode, &in) ? 0 : -1;
}

} // namespace notyvos::fs
