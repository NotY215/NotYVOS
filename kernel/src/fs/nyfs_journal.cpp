#include <kernel/fs/nyfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>

namespace notyvos::fs
{

namespace
{

constexpr u32 kJMagic = 0x4A524E4Cu;
constexpr u32 kJBlocksPerEntry = 2;

block::BlockDevice* j_dev = nullptr;
u32 j_start = 0;
u32 j_blocks = 0;
u32 j_head = 0;

struct JHeader
{
    u32 magic;
    u32 lba;
    u32 seq;
    u32 crc;
    u8  data_part0[496];
};

struct JTail
{
    u8  data_part1[16];
    u8  pad[496];
};

bool j_read_block(u32 lba, void* buf) noexcept
{
    return block::block_read(j_dev, lba, 1, buf) == static_cast<isize>(kNyfsBlockSize);
}

bool j_write_block(u32 lba, const void* buf) noexcept
{
    return block::block_write(j_dev, lba, 1, buf) == static_cast<isize>(kNyfsBlockSize);
}

u32 j_crc(const u8* data, usize len) noexcept
{
    u32 h = 0x811C9DC5u;
    for (usize i = 0; i < len; ++i)
    {
        h ^= data[i];
        h = h * 16777619u;
    }
    return h;
}

} // namespace

void nyfs_journal_init() noexcept
{
    j_dev = nyfs_device();
    // Journal region is configured at format time; read from superblock.
    if (!j_dev)
        return;
    u8 buf[kNyfsBlockSize];
    if (block::block_read(j_dev, 0, 1, buf) != static_cast<isize>(kNyfsBlockSize))
        return;
    NyfsSuperblock sb{};
    libk::memcpy(&sb, buf, sizeof(sb));
    j_start = sb.journal_start_lba;
    j_blocks = sb.journal_blocks;
    j_head = sb.journal_head;
    if (j_blocks == 0)
        log::write(log::Level::Warn, "nyfs-j", "journal disabled");
}

void nyfs_journal_log_write(u32 lba, const void* data) noexcept
{
    if (j_blocks == 0 || !j_dev)
        return;

    const u32 max_entries = j_blocks / kJBlocksPerEntry;
    if (j_head >= max_entries)
        j_head = 0;

    const u32 entry_lba = j_start + j_head * kJBlocksPerEntry;
    JHeader h{};
    h.magic = kJMagic;
    h.lba = lba;
    h.seq = j_head;
    const u8* src = static_cast<const u8*>(data);
    libk::memcpy(h.data_part0, src, 496);
    h.crc = j_crc(src, 512);
    u8 block0[kNyfsBlockSize];
    libk::memcpy(block0, &h, sizeof(h));
    if (!j_write_block(entry_lba, block0))
        return;

    JTail t{};
    libk::memcpy(t.data_part1, src + 496, 16);
    u8 block1[kNyfsBlockSize];
    libk::memcpy(block1, &t, sizeof(t));
    if (!j_write_block(entry_lba + 1, block1))
        return;

    ++j_head;

    // Persist new head in superblock.
    NyfsSuperblock sb{};
    u8 sb_buf[kNyfsBlockSize];
    if (block::block_read(j_dev, 0, 1, sb_buf) == static_cast<isize>(kNyfsBlockSize))
    {
        libk::memcpy(&sb, sb_buf, sizeof(sb));
        sb.journal_head = j_head;
        libk::memcpy(sb_buf, &sb, sizeof(sb));
        j_write_block(0, sb_buf);
    }
}

void nyfs_journal_flush() noexcept
{
    // Redo-log model: entries are applied immediately. Flush is a no-op
    // in this baseline; a proper implementation would flush caches.
}

void nyfs_journal_recover() noexcept
{
    if (j_blocks == 0 || !j_dev)
        return;
    const u32 max_entries = j_blocks / kJBlocksPerEntry;
    u32 replayed = 0;
    for (u32 i = 0; i < max_entries; ++i)
    {
        const u32 entry_lba = j_start + i * kJBlocksPerEntry;
        u8 block0[kNyfsBlockSize];
        if (!j_read_block(entry_lba, block0))
            break;
        JHeader h{};
        libk::memcpy(&h, block0, sizeof(h));
        if (h.magic != kJMagic)
            continue;
        u8 block1[kNyfsBlockSize];
        if (!j_read_block(entry_lba + 1, block1))
            continue;
        JTail t{};
        libk::memcpy(&t, block1, sizeof(t));

        u8 data[kNyfsBlockSize];
        libk::memcpy(data, h.data_part0, 496);
        libk::memcpy(data + 496, t.data_part1, 16);
        if (j_crc(data, 512) != h.crc)
            continue;

        (void)j_write_block(h.lba, data);
        ++replayed;

        // Clear the entry.
        libk::memset(block0, 0, kNyfsBlockSize);
        (void)j_write_block(entry_lba, block0);
    }
    if (replayed > 0)
        log::write(log::Level::Warn, "nyfs-j", "replayed %u journal entries", replayed);
}

} // namespace notyvos::fs
