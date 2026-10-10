#include <kernel/fs/nyfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::fs
{

namespace
{

constexpr char kMagic[8] = {'N', 'Y', 'F', 'S', 'v', '0', '0', '3'};
constexpr u32 kBlockSize = kNyfsBlockSize;
constexpr u32 kInodeSize = kNyfsInodeSize;
constexpr u32 kInodeCount = kNyfsInodeCount;
constexpr u32 kInodePerBlock = kBlockSize / kInodeSize;
constexpr u32 kMaxName = 55;

block::BlockDevice* g_dev = nullptr;
VNode* g_root = nullptr;
u32 g_inode_table_lba = 0;
u32 g_inode_bitmap_lba = 0;
u32 g_block_bitmap_lba = 0;
u32 g_block_bitmap_blocks = 0;
u32 g_data_start = 0;
u32 g_data_blocks = 0;

// Cached bitmaps. Sized for the inode table and the maximum data region
// the superblock can describe.
u8 g_inode_bitmap[kInodeCount / 8];
u8* g_block_bitmap = nullptr;   // allocated at mount time

// Per-VNode private data.
struct NyfsPrivate
{
    u32 inode;
};
constexpr u32 kMaxVNodes = 512;
NyfsPrivate g_vnode_priv[kMaxVNodes];
u32 g_vnode_priv_used = 0;

NyfsPrivate* alloc_priv(u32 inode) noexcept
{
    if (g_vnode_priv_used >= kMaxVNodes)
        return nullptr;
    NyfsPrivate* p = &g_vnode_priv[g_vnode_priv_used++];
    p->inode = inode;
    return p;
}

// ---------------------------------------------------------------------------
// Raw block I/O
// ---------------------------------------------------------------------------

bool read_block(u32 lba, void* buf) noexcept
{
    if (!g_dev)
        return false;
    return block::block_read(g_dev, lba, 1, buf) == static_cast<isize>(kBlockSize);
}

bool write_block(u32 lba, const void* buf) noexcept
{
    if (!g_dev)
        return false;
    return block::block_write(g_dev, lba, 1, buf) == static_cast<isize>(kBlockSize);
}

// ---------------------------------------------------------------------------
// Inode access
// ---------------------------------------------------------------------------

bool read_inode(u32 idx, NyfsInode* out) noexcept
{
    if (idx >= kInodeCount)
        return false;
    const u32 per_block = kInodePerBlock;
    const u32 lba = g_inode_table_lba + idx / per_block;
    const u32 off = (idx % per_block) * kInodeSize;
    u8 buf[kBlockSize];
    if (!read_block(lba, buf))
        return false;
    libk::memcpy(out, buf + off, kInodeSize);
    return true;
}

bool write_inode(u32 idx, const NyfsInode* in) noexcept
{
    if (idx >= kInodeCount)
        return false;
    const u32 per_block = kInodePerBlock;
    const u32 lba = g_inode_table_lba + idx / per_block;
    const u32 off = (idx % per_block) * kInodeSize;
    u8 buf[kBlockSize];
    if (!read_block(lba, buf))
        return false;
    libk::memcpy(buf + off, in, kInodeSize);
    return write_block(lba, buf);
}

// ---------------------------------------------------------------------------
// Bitmap helpers
// ---------------------------------------------------------------------------

bool bitmap_test(const u8* bm, u32 i) noexcept
{
    return (bm[i / 8] & (1u << (i % 8))) != 0;
}

void bitmap_set(u8* bm, u32 i) noexcept
{
    bm[i / 8] = static_cast<u8>(bm[i / 8] | (1u << (i % 8)));
}

void bitmap_clear(u8* bm, u32 i) noexcept
{
    bm[i / 8] = static_cast<u8>(bm[i / 8] & ~(1u << (i % 8)));
}

u32 bitmap_alloc(u8* bm, u32 count) noexcept
{
    for (u32 i = 0; i < count; ++i)
    {
        if (!bitmap_test(bm, i))
        {
            bitmap_set(bm, i);
            return i;
        }
    }
    return static_cast<u32>(-1);
}

// ---------------------------------------------------------------------------
// Superblock / bitmap persistence
// ---------------------------------------------------------------------------

bool write_inode_bitmap() noexcept
{
    u8 buf[kBlockSize];
    libk::memset(buf, 0, kBlockSize);
    libk::memcpy(buf, g_inode_bitmap, sizeof(g_inode_bitmap));
    nyfs_journal_log_write(g_inode_bitmap_lba, buf);
    return write_block(g_inode_bitmap_lba, buf);
}

bool write_block_bitmap() noexcept
{
    for (u32 i = 0; i < g_block_bitmap_blocks; ++i)
    {
        u8 buf[kBlockSize];
        libk::memset(buf, 0, kBlockSize);
        const u32 bytes_left = g_data_blocks / 8u + (g_data_blocks % 8u ? 1u : 0u);
        const u32 start = i * kBlockSize;
        if (start >= bytes_left)
            break;
        const u32 take = (bytes_left - start < kBlockSize) ? (bytes_left - start) : kBlockSize;
        libk::memcpy(buf, g_block_bitmap + start, take);
        if (!write_block(g_block_bitmap_lba + i, buf))
            return false;
    }
    return true;
}

bool read_block_bitmap() noexcept
{
    for (u32 i = 0; i < g_block_bitmap_blocks; ++i)
    {
        u8 buf[kBlockSize];
        if (!read_block(g_block_bitmap_lba + i, buf))
            return false;
        const u32 bytes_left = g_data_blocks / 8u + (g_data_blocks % 8u ? 1u : 0u);
        const u32 start = i * kBlockSize;
        if (start >= bytes_left)
            break;
        const u32 take = (bytes_left - start < kBlockSize) ? (bytes_left - start) : kBlockSize;
        libk::memcpy(g_block_bitmap + start, buf, take);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Block allocation
// ---------------------------------------------------------------------------

u32 alloc_data_block() noexcept
{
    const u32 idx = bitmap_alloc(g_block_bitmap, g_data_blocks);
    if (idx == static_cast<u32>(-1))
        return 0;
    (void)write_block_bitmap();
    return g_data_start + idx;
}

// ---------------------------------------------------------------------------
// Inode allocation
// ---------------------------------------------------------------------------

u32 alloc_inode(NyfsInodeMode mode) noexcept
{
    // Skip index 0; index 1 is the root.
    for (u32 i = 2; i < kInodeCount; ++i)
    {
        if (!bitmap_test(g_inode_bitmap, i))
        {
            bitmap_set(g_inode_bitmap, i);
            NyfsInode in{};
            in.mode = static_cast<u16>(mode);
            in.links = 1;
            in.size = 0;
            in.perms = (mode == NyfsInodeMode::Dir) ? kPermDefaultDir : kPermDefaultFile;
            (void)write_inode(i, &in);
            (void)write_inode_bitmap();
            return i;
        }
    }
    return static_cast<u32>(-1);
}

void free_inode(u32 idx) noexcept
{
    if (idx < 2 || idx >= kInodeCount)
        return;
    bitmap_clear(g_inode_bitmap, idx);
    NyfsInode in{};
    (void)write_inode(idx, &in);
    (void)write_inode_bitmap();
}

// ---------------------------------------------------------------------------
// Block address resolution inside an inode
// ---------------------------------------------------------------------------

u32 inode_block_at(const NyfsInode& in, u32 logical) noexcept
{
    if (logical < kNyfsDirectBlocks)
        return in.blocks[logical];
    if (in.indirect == 0)
        return 0;
    const u32 per_block = kBlockSize / 4u;
    const u32 idx = logical - kNyfsDirectBlocks;
    if (idx >= per_block)
        return 0;
    u8 buf[kBlockSize];
    if (!read_block(in.indirect, buf))
        return 0;
    u32 lba = 0;
    libk::memcpy(&lba, buf + idx * 4u, 4);
    return lba;
}

bool set_inode_block(NyfsInode& in, u32 logical, u32 lba) noexcept
{
    if (logical < kNyfsDirectBlocks)
    {
        in.blocks[logical] = lba;
        return true;
    }
    if (in.indirect == 0)
    {
        const u32 new_block = alloc_data_block();
        if (!new_block)
            return false;
        in.indirect = new_block;
        u8 zero[kBlockSize];
        libk::memset(zero, 0, kBlockSize);
        (void)write_block(new_block, zero);
    }
    const u32 per_block = kBlockSize / 4u;
    const u32 idx = logical - kNyfsDirectBlocks;
    if (idx >= per_block)
        return false;
    u8 buf[kBlockSize];
    if (!read_block(in.indirect, buf))
        return false;
    libk::memcpy(buf + idx * 4u, &lba, 4);
    return write_block(in.indirect, buf);
}

// ---------------------------------------------------------------------------
// Generic file data read/write with allocation
// ---------------------------------------------------------------------------

isize inode_read_range(NyfsInode& in, void* buf, usize off, usize len) noexcept
{
    if (off >= in.size)
        return 0;
    const usize avail = in.size - off;
    const usize n = (len < avail) ? len : avail;
    auto* out = static_cast<u8*>(buf);
    usize done = 0;
    while (done < n)
    {
        const u32 logical = static_cast<u32>((off + done) / kBlockSize);
        const u32 into = static_cast<u32>((off + done) % kBlockSize);
        const u32 want = static_cast<u32>(n - done);
        const u32 take = (want < kBlockSize - into) ? want : (kBlockSize - into);
        const u32 lba = inode_block_at(in, logical);
        if (!lba)
        {
            libk::memset(out + done, 0, take);
            done += take;
            continue;
        }
        u8 tmp[kBlockSize];
        if (!read_block(lba, tmp))
            return static_cast<isize>(done);
        libk::memcpy(out + done, tmp + into, take);
        done += take;
    }
    return static_cast<isize>(done);
}

isize inode_write_range(NyfsInode& in, const void* buf, usize off, usize len) noexcept
{
    auto* src = static_cast<const u8*>(buf);
    usize done = 0;
    while (done < len)
    {
        const u32 logical = static_cast<u32>((off + done) / kBlockSize);
        const u32 into = static_cast<u32>((off + done) % kBlockSize);
        const u32 want = static_cast<u32>(len - done);
        const u32 take = (want < kBlockSize - into) ? want : (kBlockSize - into);
        u32 lba = inode_block_at(in, logical);
        if (!lba)
        {
            lba = alloc_data_block();
            if (!lba)
                return static_cast<isize>(done);
            if (!set_inode_block(in, logical, lba))
                return static_cast<isize>(done);
        }
        u8 tmp[kBlockSize];
        if (into != 0 || take < kBlockSize)
        {
            if (!read_block(lba, tmp))
                return static_cast<isize>(done);
        }
        else
        {
            libk::memset(tmp, 0, kBlockSize);
        }
        libk::memcpy(tmp + into, src + done, take);
        if (!write_block(lba, tmp))
            return static_cast<isize>(done);
        done += take;
    }
    if (off + len > in.size)
        in.size = static_cast<u32>(off + len);
    return static_cast<isize>(done);
}

// ---------------------------------------------------------------------------
// Directory operations
//
// A directory inode's data is a flat array of NyfsDirEntry records. We
// walk it block by block, reading entries via inode_read_range.
// ---------------------------------------------------------------------------

bool dir_lookup(NyfsInode& dir, const char* name, u32* out_inode) noexcept
{
    const u32 count = dir.size / kNyfsDirEntrySize;
    for (u32 i = 0; i < count; ++i)
    {
        NyfsDirEntry e{};
        if (inode_read_range(dir, &e, i * kNyfsDirEntrySize, kNyfsDirEntrySize) !=
            static_cast<isize>(kNyfsDirEntrySize))
            return false;
        if (e.inode == 0 || e.name_len == 0)
            continue;
        if (libk::strncmp(e.name, name, e.name_len) == 0 && name[e.name_len] == 0)
        {
            *out_inode = e.inode;
            return true;
        }
    }
    return false;
}

bool dir_insert(NyfsInode& dir, const char* name, u32 inode, u8 type) noexcept
{
    const usize name_len = libk::strlen(name);
    if (name_len == 0 || name_len > kMaxName)
        return false;

    // Find a free slot first.
    const u32 count = dir.size / kNyfsDirEntrySize;
    for (u32 i = 0; i < count; ++i)
    {
        NyfsDirEntry e{};
        if (inode_read_range(dir, &e, i * kNyfsDirEntrySize, kNyfsDirEntrySize) !=
            static_cast<isize>(kNyfsDirEntrySize))
            return false;
        if (e.inode != 0)
            continue;
        libk::memset(&e, 0, sizeof(e));
        e.inode = inode;
        e.type = type;
        e.name_len = static_cast<u8>(name_len);
        libk::memcpy(e.name, name, name_len);
        return inode_write_range(dir, &e, i * kNyfsDirEntrySize, kNyfsDirEntrySize) ==
               static_cast<isize>(kNyfsDirEntrySize);
    }

    // Append a new entry.
    NyfsDirEntry e{};
    libk::memset(&e, 0, sizeof(e));
    e.inode = inode;
    e.type = type;
    e.name_len = static_cast<u8>(name_len);
    libk::memcpy(e.name, name, name_len);
    const usize off = count * kNyfsDirEntrySize;
    return inode_write_range(dir, &e, off, kNyfsDirEntrySize) ==
           static_cast<isize>(kNyfsDirEntrySize);
}

bool dir_remove(NyfsInode& dir, const char* name) noexcept
{
    const u32 count = dir.size / kNyfsDirEntrySize;
    for (u32 i = 0; i < count; ++i)
    {
        NyfsDirEntry e{};
        if (inode_read_range(dir, &e, i * kNyfsDirEntrySize, kNyfsDirEntrySize) !=
            static_cast<isize>(kNyfsDirEntrySize))
            return false;
        if (e.inode == 0 || e.name_len == 0)
            continue;
        if (libk::strncmp(e.name, name, e.name_len) == 0 && name[e.name_len] == 0)
        {
            e.inode = 0;
            e.name_len = 0;
            return inode_write_range(dir, &e, i * kNyfsDirEntrySize, kNyfsDirEntrySize) ==
                   static_cast<isize>(kNyfsDirEntrySize);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// VNodeOps for NYFS
// ---------------------------------------------------------------------------

isize nyfs_readdir_op(VNode* n, usize idx, DirEntry* out) noexcept
{
    if (!n || !n->priv)
        return 0;
    const auto* p = static_cast<const NyfsPrivate*>(n->priv);
    NyfsInode in{};
    if (!read_inode(p->inode, &in))
        return 0;
    if (in.mode != static_cast<u16>(NyfsInodeMode::Dir))
        return 0;

    const u32 count = in.size / kNyfsDirEntrySize;
    u32 seen = 0;
    for (u32 i = 0; i < count; ++i)
    {
        NyfsDirEntry e{};
        if (inode_read_range(in, &e, i * kNyfsDirEntrySize, kNyfsDirEntrySize) !=
            static_cast<isize>(kNyfsDirEntrySize))
            break;
        if (e.inode == 0 || e.name_len == 0)
            continue;
        // Skip the `.` and `..` entries on disk: they are synthesised at
        // the layer above (Explorer and VFS) so directories always
        // contain real children plus the two implicit entries.
        if ((e.name_len == 1 && e.name[0] == '.') ||
            (e.name_len == 2 && e.name[0] == '.' && e.name[1] == '.'))
            continue;

        if (seen == idx)
        {
            libk::memset(out, 0, sizeof(*out));
            u32 k = 0;
            while (k < e.name_len && k < sizeof(out->name) - 1)
            {
                out->name[k] = e.name[k];
                ++k;
            }
            out->name[k] = 0;
            out->type = (e.type == 1) ? VType::Dir : VType::File;
            out->ino = e.inode;
            NyfsInode child{};
            if (read_inode(e.inode, &child))
                out->size = child.size;
            return 1;
        }
        ++seen;
    }
    return 0;
}

isize nyfs_read_op(VNode* n, void* buf, usize off, usize len) noexcept
{
    if (!n || !n->priv)
        return -1;
    const auto* p = static_cast<const NyfsPrivate*>(n->priv);
    NyfsInode in{};
    if (!read_inode(p->inode, &in))
        return -1;
    return inode_read_range(in, buf, off, len);
}

isize nyfs_write_op(VNode* n, const void* buf, usize off, usize len) noexcept
{
    if (!n || !n->priv)
        return -1;
    const auto* p = static_cast<const NyfsPrivate*>(n->priv);
    NyfsInode in{};
    if (!read_inode(p->inode, &in))
        return -1;
    const isize r = inode_write_range(in, buf, off, len);
    if (r > 0)
        (void)write_inode(p->inode, &in);
    return r;
}

isize nyfs_size_op(VNode* n) noexcept
{
    if (!n || !n->priv)
        return 0;
    const auto* p = static_cast<const NyfsPrivate*>(n->priv);
    NyfsInode in{};
    if (!read_inode(p->inode, &in))
        return 0;
    return static_cast<isize>(in.size);
}

VNodeOps g_file_ops = {nyfs_read_op, nyfs_write_op, nullptr, nullptr, nyfs_size_op};
VNodeOps g_dir_ops  = {nullptr, nullptr, nyfs_readdir_op, nullptr, nullptr};

// ---------------------------------------------------------------------------
// VNode construction
// ---------------------------------------------------------------------------

VNode* make_vnode(u32 inode, VType type, const char* name) noexcept
{
    VNode* vn = vnode_alloc(inode, type, name);
    if (!vn)
        return nullptr;
    NyfsPrivate* p = alloc_priv(inode);
    if (!p)
        return vn;
    vn->priv = p;
    vn->ops = (type == VType::Dir) ? &g_dir_ops : &g_file_ops;
    return vn;
}

void populate_dir(VNode* dir, u32 inode) noexcept
{
    NyfsInode in{};
    if (!read_inode(inode, &in))
        return;
    if (in.mode != static_cast<u16>(NyfsInodeMode::Dir))
        return;

    const u32 count = in.size / kNyfsDirEntrySize;
    for (u32 i = 0; i < count; ++i)
    {
        NyfsDirEntry e{};
        if (inode_read_range(in, &e, i * kNyfsDirEntrySize, kNyfsDirEntrySize) !=
            static_cast<isize>(kNyfsDirEntrySize))
            break;
        if (e.inode == 0 || e.name_len == 0)
            continue;
        char name[64];
        u32 k = 0;
        while (k < e.name_len && k < sizeof(name) - 1)
        {
            name[k] = e.name[k];
            ++k;
        }
        name[k] = 0;
        if (libk::strcmp(name, ".") == 0 || libk::strcmp(name, "..") == 0)
            continue;
        VNode* child = make_vnode(e.inode, (e.type == 1) ? VType::Dir : VType::File, name);
        if (child)
            vnode_attach(dir, child);
    }
}

// ---------------------------------------------------------------------------
// Format
// ---------------------------------------------------------------------------

void format() noexcept
{
    // Compute layout.
    const u32 total = static_cast<u32>(g_dev->sector_count);
    const u32 inode_bitmap_bytes = kInodeCount / 8u;
    const u32 inode_bitmap_blocks = (inode_bitmap_bytes + kBlockSize - 1) / kBlockSize;
    const u32 inode_table_blocks = (kInodeCount * kInodeSize + kBlockSize - 1) / kBlockSize;
    const u32 journal_blocks = 128;

    u32 block_bitmap_lba = 1 + inode_bitmap_blocks;
    u32 journal_start = block_bitmap_lba + 1;
    u32 inode_table_lba = journal_start + journal_blocks;
    u32 data_start = inode_table_lba + inode_table_blocks;
    u32 data_blocks = (total > data_start) ? (total - data_start) : 0;
    u32 needed_bitmap_blocks = (data_blocks / 8u + kBlockSize - 1) / kBlockSize;
    if (needed_bitmap_blocks == 0)
        needed_bitmap_blocks = 1;

    journal_start = block_bitmap_lba + needed_bitmap_blocks;
    inode_table_lba = journal_start + journal_blocks;
    data_start = inode_table_lba + inode_table_blocks;
    data_blocks = (total > data_start) ? (total - data_start) : 0;

    g_inode_bitmap_lba = 1;
    g_block_bitmap_lba = block_bitmap_lba;
    g_block_bitmap_blocks = needed_bitmap_blocks;
    g_inode_table_lba = inode_table_lba;
    g_data_start = data_start;
    g_data_blocks = data_blocks;

    libk::memset(g_inode_bitmap, 0, sizeof(g_inode_bitmap));
    if (!g_block_bitmap)
        g_block_bitmap = static_cast<u8*>(mm::Heap::allocate(g_block_bitmap_blocks * kBlockSize));
    if (g_block_bitmap)
        libk::memset(g_block_bitmap, 0, g_block_bitmap_blocks * kBlockSize);

    // Reserve inode 0 and 1.
    bitmap_set(g_inode_bitmap, 0);
    bitmap_set(g_inode_bitmap, 1);

    // Write superblock.
    NyfsSuperblock sb{};
    libk::memcpy(sb.magic, kMagic, kNyfsMagicSize);
    sb.block_size = kBlockSize;
    sb.total_blocks = total;
    sb.inode_count = kInodeCount;
    sb.inode_bitmap_lba = g_inode_bitmap_lba;
    sb.block_bitmap_lba = g_block_bitmap_lba;
    sb.block_bitmap_blocks = g_block_bitmap_blocks;
    sb.inode_table_lba = g_inode_table_lba;
    sb.inode_table_blocks = inode_table_blocks;
    sb.data_start_lba = g_data_start;
    sb.data_blocks = g_data_blocks;
    sb.root_inode = kNyfsRootInode;
    sb.free_blocks = g_data_blocks;
    sb.free_inodes = kInodeCount - 2;
    sb.journal_start_lba = journal_start;
    sb.journal_blocks = journal_blocks;
    sb.journal_head = 0;

    u8 buf[kBlockSize];
    libk::memset(buf, 0, kBlockSize);
    libk::memcpy(buf, &sb, sizeof(sb));
    (void)write_block(0, buf);

    // Zero the inode table.
    for (u32 i = 0; i < inode_table_blocks; ++i)
    {
        libk::memset(buf, 0, kBlockSize);
        (void)write_block(g_inode_table_lba + i, buf);
    }

    // Create the root inode.
    NyfsInode root{};
    root.mode = static_cast<u16>(NyfsInodeMode::Dir);
    root.links = 1;
    root.size = 0;
    root.perms = kPermDefaultDir;

    // Root directory starts empty. `.` and `..` are synthesised at read
    // time and are never stored on disk.

    (void)write_inode_bitmap();
    (void)write_block_bitmap();

    log::write(log::Level::Info, "nyfs",
               "formatted: %u blocks, %u inodes, data from LBA %u",
               total, kInodeCount, g_data_start);
}

bool read_superblock(NyfsSuperblock* sb) noexcept
{
    u8 buf[kBlockSize];
    if (!read_block(0, buf))
        return false;
    libk::memcpy(sb, buf, sizeof(*sb));
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

block::BlockDevice* nyfs_device()
{
    return g_dev;
}

VNode* nyfs_mount(block::BlockDevice* dev)
{
    if (!dev)
        return nullptr;
    g_dev = dev;

    NyfsSuperblock sb{};
    if (!read_superblock(&sb))
    {
        log::write(log::Level::Error, "nyfs", "superblock read failed");
        return nullptr;
    }

    const bool magic_ok = libk::memcmp(sb.magic, kMagic, kNyfsMagicSize) == 0;
    if (!magic_ok || sb.block_size != kBlockSize)
    {
        log::write(log::Level::Info, "nyfs", "no valid v2 superblock, formatting");
        format();
        if (!read_superblock(&sb))
            return nullptr;
    }

    g_inode_bitmap_lba = sb.inode_bitmap_lba;
    g_block_bitmap_lba = sb.block_bitmap_lba;
    g_block_bitmap_blocks = sb.block_bitmap_blocks;
    g_inode_table_lba = sb.inode_table_lba;
    g_data_start = sb.data_start_lba;
    g_data_blocks = sb.data_blocks;

    if (!g_block_bitmap)
        g_block_bitmap = static_cast<u8*>(mm::Heap::allocate(g_block_bitmap_blocks * kBlockSize));
    if (!g_block_bitmap)
        return nullptr;

    // Load bitmaps.
    {
        u8 buf[kBlockSize];
        if (!read_block(g_inode_bitmap_lba, buf))
            return nullptr;
        libk::memcpy(g_inode_bitmap, buf, sizeof(g_inode_bitmap));
    }
    (void)read_block_bitmap();
    nyfs_journal_init();
    nyfs_journal_recover();
    (void)nyfs_fsck(true);

    log::write(log::Level::Info, "nyfs", "mounted: %u blocks, %u inodes, %u free blocks",
               sb.total_blocks, sb.inode_count, sb.free_blocks);

    // Build the root VNode.
    g_root = make_vnode(kNyfsRootInode, VType::Dir, "/");
    if (!g_root)
        return nullptr;
    populate_dir(g_root, kNyfsRootInode);
    return g_root;
}

void nyfs_rescan()
{
    if (!g_root)
        return;
    // Free existing children and re-populate.
    VNode* c = g_root->children;
    while (c)
    {
        VNode* next = c->next;
        mm::Heap::deallocate(c);
        c = next;
    }
    g_root->children = nullptr;
    populate_dir(g_root, kNyfsRootInode);
}

isize nyfs_read(VNode* n, void* buf, usize off, usize len)
{
    return nyfs_read_op(n, buf, off, len);
}

isize nyfs_write(VNode* n, const void* buf, usize off, usize len)
{
    return nyfs_write_op(n, buf, off, len);
}

isize nyfs_size(VNode* n)
{
    return nyfs_size_op(n);
}

int nyfs_create(const char* name)
{
    if (!g_root || !name || !name[0])
        return -1;
    VNode* v = nyfs_create_in(g_root, name, VType::File);
    return v ? 0 : -1;
}

int nyfs_unlink(const char* name)
{
    if (!g_root || !name || !name[0])
        return -1;
    NyfsInode dir{};
    if (!read_inode(kNyfsRootInode, &dir))
        return -1;
    u32 inode = 0;
    if (!dir_lookup(dir, name, &inode))
        return -1;
    if (!dir_remove(dir, name))
        return -1;
    (void)write_inode(kNyfsRootInode, &dir);
    free_inode(inode);
    nyfs_rescan();
    return 0;
}

int nyfs_rename(const char* old_name, const char* new_name)
{
    if (!g_root || !old_name || !new_name)
        return -1;
    NyfsInode dir{};
    if (!read_inode(kNyfsRootInode, &dir))
        return -1;
    u32 inode = 0;
    if (!dir_lookup(dir, old_name, &inode))
        return -1;
    u32 dummy = 0;
    if (dir_lookup(dir, new_name, &dummy))
        return -1;   // target exists
    if (!dir_remove(dir, old_name))
        return -1;
    if (!dir_insert(dir, new_name, inode, 0))
        return -1;
    (void)write_inode(kNyfsRootInode, &dir);
    nyfs_rescan();
    return 0;
}

VNode* nyfs_create_in(VNode* parent, const char* name, VType type)
{
    if (!parent || !name || !name[0])
        return nullptr;
    if (!parent->priv)
        return nullptr;
    const auto* pp = static_cast<const NyfsPrivate*>(parent->priv);

    NyfsInode dir{};
    if (!read_inode(pp->inode, &dir))
        return nullptr;
    if (dir.mode != static_cast<u16>(NyfsInodeMode::Dir))
        return nullptr;

    u32 existing = 0;
    if (dir_lookup(dir, name, &existing))
        return nullptr;

    const NyfsInodeMode mode =
        (type == VType::Dir) ? NyfsInodeMode::Dir : NyfsInodeMode::File;
    const u32 inode = alloc_inode(mode);
    if (inode == static_cast<u32>(-1))
        return nullptr;

    if (!dir_insert(dir, name, inode, type == VType::Dir ? 1 : 0))
    {
        free_inode(inode);
        return nullptr;
    }
    (void)write_inode(pp->inode, &dir);

    VNode* child = make_vnode(inode, type, name);
    if (child)
        vnode_attach(parent, child);
    return child;
}

int nyfs_mkdir(VNode* parent, const char* name)
{
    return nyfs_create_in(parent, name, VType::Dir) ? 0 : -1;
}

int nyfs_rmdir(VNode* parent, const char* name)
{
    if (!parent || !parent->priv || !name)
        return -1;
    const auto* pp = static_cast<const NyfsPrivate*>(parent->priv);
    NyfsInode dir{};
    if (!read_inode(pp->inode, &dir))
        return -1;
    u32 inode = 0;
    if (!dir_lookup(dir, name, &inode))
        return -1;
    NyfsInode child{};
    if (!read_inode(inode, &child))
        return -1;
    if (child.mode != static_cast<u16>(NyfsInodeMode::Dir))
        return -1;
    // Refuse if the directory still has real children.
    const u32 count = child.size / kNyfsDirEntrySize;
    if (count > 0)
        return -1;
    if (!dir_remove(dir, name))
        return -1;
    (void)write_inode(pp->inode, &dir);
    free_inode(inode);
    nyfs_rescan();
    return 0;
}

NyfsStats nyfs_stats()
{
    NyfsStats s{};
    s.total_blocks = g_data_blocks;
    s.free_blocks = 0;
    for (u32 i = 0; i < g_data_blocks; ++i)
        if (!bitmap_test(g_block_bitmap, i))
            ++s.free_blocks;
    s.total_inodes = kInodeCount;
    s.free_inodes = 0;
    for (u32 i = 2; i < kInodeCount; ++i)
        if (!bitmap_test(g_inode_bitmap, i))
            ++s.free_inodes;
    s.block_size = kBlockSize;
    return s;
}

bool nyfs_owns_vnode(const VNode* n) noexcept
{
    if (!n || !n->priv)
        return false;
    const auto* p = static_cast<const NyfsPrivate*>(n->priv);
    return p >= g_vnode_priv && p < g_vnode_priv + kMaxVNodes;
}

} // namespace notyvos::fs
