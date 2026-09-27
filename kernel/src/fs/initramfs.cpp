#include <kernel/fs/initramfs.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/log.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::fs
{

namespace
{

struct UstarHeader
{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

static_assert(sizeof(UstarHeader) == 512, "ustar header must be 512 bytes");

u64 parse_octal(const char* s, usize n)
{
    u64 v = 0;
    for (usize i = 0; i < n; ++i)
    {
        if (s[i] < '0' || s[i] > '7')
            break;
        v = v * 8 + static_cast<u64>(s[i] - '0');
    }
    return v;
}

struct MemFile
{
    const u8* data;
    usize size;
};

isize mem_read(VNode* n, void* buf, usize off, usize len)
{
    auto* mf = static_cast<MemFile*>(n->priv);
    if (!mf)
        return -1;
    if (off >= mf->size)
        return 0;
    usize avail = mf->size - off;
    usize n_copy = (len < avail) ? len : avail;
    libk::memcpy(buf, mf->data + off, n_copy);
    return static_cast<isize>(n_copy);
}
isize mem_size(VNode* n)
{
    auto* mf = static_cast<MemFile*>(n->priv);
    return mf ? static_cast<isize>(mf->size) : 0;
}
isize dir_readdir(VNode* n, usize idx, DirEntry* out)
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
}

VNodeOps g_file_ops{mem_read, nullptr, nullptr, nullptr, mem_size};
VNodeOps g_dir_ops{nullptr, nullptr, dir_readdir, nullptr, nullptr};

VNode* make_dir(const char* name)
{
    auto* d = vnode_alloc(0, VType::Dir, name);
    if (d)
        d->ops = &g_dir_ops;
    return d;
}

VNode* make_file(const char* name, const u8* data, usize size)
{
    auto* f = vnode_alloc(0, VType::File, name);
    if (!f)
        return nullptr;
    f->ops = &g_file_ops;
    auto* mf = static_cast<MemFile*>(mm::Heap::allocate(sizeof(MemFile)));
    if (!mf)
        return nullptr;
    mf->data = data;
    mf->size = size;
    f->priv = mf;
    return f;
}

VNode* walk_parent(VNode* root, const char* path, char* out_leaf, usize leaf_max)
{
    char comp[64];
    char components[16][64];
    u32 count = 0;
    const char* p = path;

    while (*p)
    {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        usize n = 0;
        while (*p && *p != '/' && n < sizeof(comp) - 1)
            comp[n++] = *p++;
        comp[n] = 0;
        if (count < 16)
        {
            libk::strncpy(components[count], comp, 63);
            components[count][63] = 0;
            count++;
        }
    }
    if (count == 0)
        return nullptr;
    libk::strncpy(out_leaf, components[count - 1], leaf_max - 1);
    out_leaf[leaf_max - 1] = 0;

    VNode* cur = root;
    for (u32 i = 0; i + 1 < count; ++i)
    {
        VNode* next = vnode_find_child(cur, components[i]);
        if (!next)
        {
            next = make_dir(components[i]);
            if (!next)
                return nullptr;
            vnode_attach(cur, next);
        }
        cur = next;
    }
    return cur;
}

} // namespace

VNode* initramfs_mount(const void* image, usize size)
{
    auto* root = make_dir("/");
    if (!root)
        return nullptr;

    const auto* p = static_cast<const u8*>(image);
    const auto* end = p + size;

    while (p + sizeof(UstarHeader) <= end)
    {
        const auto* h = reinterpret_cast<const UstarHeader*>(p);
        if (h->name[0] == 0)
            break;

        const u64 fsize = parse_octal(h->size, sizeof(h->size));
        const char typeflag = h->typeflag;

        char full[256];
        if (h->prefix[0])
        {
            usize pl = 0;
            while (pl < sizeof(h->prefix) && h->prefix[pl])
                pl++;
            libk::memcpy(full, h->prefix, pl);
            full[pl] = '/';
            usize nl = 0;
            while (nl < sizeof(h->name) && h->name[nl])
                nl++;
            libk::memcpy(full + pl + 1, h->name, nl);
            full[pl + 1 + nl] = 0;
        }
        else
        {
            usize nl = 0;
            while (nl < sizeof(h->name) && h->name[nl])
                nl++;
            libk::memcpy(full, h->name, nl);
            full[nl] = 0;
        }

        const char* name = full;
        while (name[0] == '.' && name[1] == '/')
            name += 2;
        while (name[0] == '/')
            name++;

        usize nlen = libk::strlen(name);
        char clean[256];
        libk::memcpy(clean, name, nlen);
        clean[nlen] = 0;

        if (nlen > 0)
        {
            char leaf[64];
            VNode* parent = walk_parent(root, clean, leaf, sizeof(leaf));
            if (parent)
            {
                if (typeflag == '5')
                {
                    if (!vnode_find_child(parent, leaf))
                    {
                        VNode* d = make_dir(leaf);
                        if (d)
                            vnode_attach(parent, d);
                    }
                }
                else if (typeflag == '0' || typeflag == 0)
                {
                    const u8* data = p + sizeof(UstarHeader);
                    VNode* f = make_file(leaf, data, static_cast<usize>(fsize));
                    if (f)
                        vnode_attach(parent, f);
                }
            }
        }

        p += sizeof(UstarHeader) + ((fsize + 511) / 512) * 512;
    }

    log::write(log::Level::Info, "initramfs", "mounted %llu bytes",
               static_cast<unsigned long long>(size));
    return root;
}

} // namespace notyvos::fs
