#include <kernel/fs/nyfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::fs
{

namespace
{

constexpr u32 kBlockSize = 512;
constexpr u32 kMaxFiles = 64;
constexpr u64 kDataStart = 1 + kMaxFiles;
constexpr char kMagic[8] = {'N', 'Y', 'F', 'S', 'v', '0', '0', '1'};

struct Superblock
{
    char magic[8];
    u32 block_size;
    u32 total_blocks;
    u32 file_count;
    u32 reserved;
    u8 pad[512 - 24];
} __attribute__((packed));

struct FileEntry
{
    u8 name_len;
    char name[63];
    u32 size;
    u32 data_lba;
    u8 pad[512 - 72];
} __attribute__((packed));

static_assert(sizeof(Superblock) == 512, "superblock must fit a sector");
static_assert(sizeof(FileEntry) == 512, "file entry must fit a sector");

block::BlockDevice* g_dev = nullptr;
VNode* g_root = nullptr;

struct NyfsFile
{
    u32 entry_index;
};


isize nyfs_readdir(VNode* n, usize idx, DirEntry* out)
{
    usize i = 0;
    for (VNode* c = n->children; c; c = c->next, ++i)
    {
        if (i == idx)
        {
            libk::strncpy(out->name, c->name, sizeof(out->name) - 1);
            out->name[sizeof(out->name) - 1] = 0;
            out->type = c->type;
            out->ino = c->ino;
            out->size = 0;
            if (c->ops && c->ops->size)
                out->size = static_cast<u64>(c->ops->size(c));
            return 1;
        }
    }
    return 0;
};
VNodeOps g_nyfs_file_ops = {nyfs_read, nyfs_write, nullptr, nullptr, nyfs_size};
VNodeOps g_nyfs_dir_ops = {nullptr, nullptr, nyfs_readdir, nullptr, nullptr};

bool read_sector(u64 lba, void* buf)
{
    if (!g_dev)
        return false;
    return block::block_read(g_dev, lba, 1, buf) == static_cast<isize>(kBlockSize);
}

bool write_sector(u64 lba, const void* buf)
{
    if (!g_dev)
        return false;
    return block::block_write(g_dev, lba, 1, buf) == static_cast<isize>(kBlockSize);
}

bool read_entry(u32 idx, FileEntry* out)
{
    if (idx >= kMaxFiles)
        return false;
    return read_sector(1 + idx, out);
}

bool write_entry(u32 idx, const FileEntry* e)
{
    if (idx >= kMaxFiles)
        return false;
    return write_sector(1 + idx, e);
}

void format_disk()
{
    Superblock sb{};
    for (u32 i = 0; i < 8; ++i)
        sb.magic[i] = kMagic[i];
    sb.block_size = kBlockSize;
    sb.total_blocks = static_cast<u32>(g_dev->sector_count);
    sb.file_count = 0;
    write_sector(0, &sb);

    FileEntry empty{};
    for (u32 i = 0; i < kMaxFiles; ++i)
        write_entry(i, &empty);

    log::write(log::Level::Info, "nyfs", "formatted %llu-block device",
               static_cast<unsigned long long>(g_dev->sector_count));
}

void make_dir_root()
{
    g_root = vnode_alloc(1, VType::Dir, "/");
    if (g_root)
        g_root->ops = &g_nyfs_dir_ops;
}

void rebuild_children()
{
    if (!g_root)
        return;

    /* Free the old children list. */
    VNode* c = g_root->children;
    while (c)
    {
        VNode* next = c->next;
        if (c->priv)
            mm::Heap::deallocate(c->priv);
        mm::Heap::deallocate(c);
        c = next;
    }
    g_root->children = nullptr;

    for (u32 i = 0; i < kMaxFiles; ++i)
    {
        FileEntry e{};
        if (!read_entry(i, &e))
            continue;
        if (e.name_len == 0 || e.name_len > 63)
            continue;

        char name[64];
        libk::memcpy(name, e.name, e.name_len);
        name[e.name_len] = 0;

        VNode* f = vnode_alloc(100 + i, VType::File, name);
        if (!f)
            continue;
        f->ops = &g_nyfs_file_ops;

        auto* nf = static_cast<NyfsFile*>(mm::Heap::allocate(sizeof(NyfsFile)));
        if (!nf)
            continue;
        nf->entry_index = i;
        f->priv = nf;

        vnode_attach(g_root, f);
    }
}

} // namespace

block::BlockDevice* nyfs_device()
{
    return g_dev;
}

VNode* nyfs_mount(block::BlockDevice* dev)
{
    if (!dev)
        return nullptr;
    g_dev = dev;

    Superblock sb{};
    if (!read_sector(0, &sb))
    {
        log::write(log::Level::Error, "nyfs", "superblock read failed");
        return nullptr;
    }

    const bool magic_ok = libk::memcmp(sb.magic, kMagic, 8) == 0;
    if (!magic_ok || sb.block_size != kBlockSize)
    {
        log::write(log::Level::Info, "nyfs", "no valid superblock, formatting");
        format_disk();
    }
    else
    {
        log::write(log::Level::Info, "nyfs", "mounted: %llu blocks, %llu files",
                   static_cast<unsigned long long>(sb.total_blocks),
                   static_cast<unsigned long long>(sb.file_count));
    }

    make_dir_root();
    rebuild_children();
    return g_root;
}

void nyfs_rescan()
{
    rebuild_children();
}

isize nyfs_size(VNode* n)
{
    if (!n || !n->priv)
        return 0;
    auto* nf = static_cast<NyfsFile*>(n->priv);
    FileEntry e{};
    if (!read_entry(nf->entry_index, &e))
        return 0;
    return static_cast<isize>(e.size);
}

isize nyfs_read(VNode* n, void* buf, usize off, usize len)
{
    if (!n || !n->priv)
        return -1;
    auto* nf = static_cast<NyfsFile*>(n->priv);
    FileEntry e{};
    if (!read_entry(nf->entry_index, &e))
        return -1;
    if (e.name_len == 0)
        return -1;
    if (off >= e.size)
        return 0;

    const usize avail = e.size - off;
    const usize n_copy = (len < avail) ? len : avail;

    auto* out = static_cast<u8*>(buf);
    usize done = 0;
    while (done < n_copy)
    {
        const u64 abs_off = off + done;
        const u64 sector = e.data_lba + abs_off / kBlockSize;
        const u64 into = abs_off % kBlockSize;
        const u64 want = n_copy - done;
        const u64 chunk = (want < (kBlockSize - into)) ? want : (kBlockSize - into);

        u8 tmp[kBlockSize];
        if (!read_sector(sector, tmp))
            return -1;
        libk::memcpy(out + done, tmp + into, static_cast<usize>(chunk));
        done += static_cast<usize>(chunk);
    }
    return static_cast<isize>(done);
}

isize nyfs_write(VNode* n, const void* buf, usize off, usize len)
{
    if (!n || !n->priv)
        return -1;
    auto* nf = static_cast<NyfsFile*>(n->priv);
    FileEntry e{};
    if (!read_entry(nf->entry_index, &e))
        return -1;
    if (e.name_len == 0)
        return -1;

    /* Allocate data sectors on first write. */
    if (e.data_lba == 0)
    {
        u64 next_free = kDataStart;
        for (u32 i = 0; i < kMaxFiles; ++i)
        {
            FileEntry o{};
            if (!read_entry(i, &o))
                continue;
            if (o.name_len == 0)
                continue;
            const u64 end = o.data_lba + (o.size + kBlockSize - 1) / kBlockSize;
            if (end > next_free)
                next_free = end;
        }
        e.data_lba = static_cast<u32>(next_free);
    }

    const auto* src = static_cast<const u8*>(buf);
    usize done = 0;
    while (done < len)
    {
        const u64 abs_off = off + done;
        const u64 sector = e.data_lba + abs_off / kBlockSize;
        const u64 into = abs_off % kBlockSize;
        const u64 want = len - done;
        const u64 chunk = (want < (kBlockSize - into)) ? want : (kBlockSize - into);

        u8 tmp[kBlockSize];
        if (into != 0 || chunk < kBlockSize)
        {
            if (!read_sector(sector, tmp))
                return -1;
        }
        else
        {
            libk::memset(tmp, 0, kBlockSize);
        }
        libk::memcpy(tmp + into, src + done, static_cast<usize>(chunk));
        if (!write_sector(sector, tmp))
            return -1;
        done += static_cast<usize>(chunk);
    }

    const usize end_off = off + len;
    if (end_off > e.size)
    {
        e.size = static_cast<u32>(end_off);
        write_entry(nf->entry_index, &e);
    }

    return static_cast<isize>(len);
}

int nyfs_create(const char* name)
{
    if (!g_dev || !name)
        return -1;
    const usize nlen = libk::strlen(name);
    if (nlen == 0 || nlen > 63)
        return -1;

    for (u32 i = 0; i < kMaxFiles; ++i)
    {
        FileEntry e{};
        if (!read_entry(i, &e))
            continue;
        if (e.name_len == nlen && libk::memcmp(e.name, name, nlen) == 0)
        {
            return 0; /* already exists, treat as success */
        }
    }

    for (u32 i = 0; i < kMaxFiles; ++i)
    {
        FileEntry e{};
        if (!read_entry(i, &e))
            continue;
        if (e.name_len == 0)
        {
            e.name_len = static_cast<u8>(nlen);
            libk::memset(e.name, 0, sizeof(e.name));
            libk::memcpy(e.name, name, nlen);
            e.size = 0;
            e.data_lba = 0;
            if (!write_entry(i, &e))
                return -1;
            nyfs_rescan();
            return 0;
        }
    }
    return -1;
}

int nyfs_unlink(const char* name)
{
    if (!g_dev || !name)
        return -1;
    const usize nlen = libk::strlen(name);
    for (u32 i = 0; i < kMaxFiles; ++i)
    {
        FileEntry e{};
        if (!read_entry(i, &e))
            continue;
        if (e.name_len == nlen && libk::memcmp(e.name, name, nlen) == 0)
        {
            e.name_len = 0;
            if (!write_entry(i, &e))
                return -1;
            nyfs_rescan();
            return 0;
        }
    }
    return -1;
}

} // namespace notyvos::fs
