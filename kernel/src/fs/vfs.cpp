#include <kernel/fs/vfs.hpp>
#include <kernel/fs/nyfs.hpp>
#include <kernel/libk/string.hpp>

namespace notyvos::fs
{

namespace
{
VNode* g_root = nullptr;
}

void vfs_init()
{
    g_root = nullptr;
}
void vfs_mount_root(VNode* root)
{
    g_root = root;
}
VNode* vfs_root()
{
    return g_root;
}

static VNode* resolve_from(VNode* start, const char* path)
{
    VNode* cur = start;
    const char* p = path;
    char comp[64];
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
        if (libk::strcmp(comp, ".") == 0)
            continue;
        if (libk::strcmp(comp, "..") == 0)
        {
            if (cur->parent)
                cur = cur->parent;
            continue;
        }
        cur = vnode_find_child(cur, comp);
        if (!cur)
            return nullptr;
    }
    return cur;
}

VNode* vfs_lookup(const char* path, const char* cwd)
{
    if (!path || !g_root)
        return nullptr;
    VNode* start;
    const char* p;
    if (path[0] == '/')
    {
        start = g_root;
        p = path;
    }
    else
    {
        start = (cwd && cwd[0] == '/') ? resolve_from(g_root, cwd) : g_root;
        if (!start)
            start = g_root;
        p = path;
    }
    return resolve_from(start, p);
}

int vfs_create(VNode* parent, const char* name) noexcept
{
    if (!parent || !name || !name[0])
        return -1;

    // NYFS is mounted under a vnode whose name is "disk".
    if (libk::strcmp(parent->name, "disk") == 0 && parent->type == VType::Dir)
    {
        return nyfs_create(name);
    }
    // Initramfs is read-only.
    return -1;
}

int vfs_unlink(VNode* node) noexcept
{
    if (!node)
        return -1;
    if (!node->parent)
        return -1;
    if (libk::strcmp(node->parent->name, "disk") != 0)
        return -1;
    return nyfs_unlink(node->name);
}

int vfs_rename(VNode* node, const char* new_name) noexcept
{
    if (!node || !new_name || !new_name[0])
        return -1;
    if (!node->parent)
        return -1;
    if (libk::strcmp(node->parent->name, "disk") != 0)
        return -1;
    return nyfs_rename(node->name, new_name);
}

namespace
{
const void* g_fw_data = nullptr;
usize g_fw_size = 0;
} // namespace

void vfs_register_firmware(const void* data, usize size) noexcept
{
    g_fw_data = data;
    g_fw_size = size;
}

const void* vfs_firmware(usize* out_size) noexcept
{
    if (out_size)
        *out_size = g_fw_size;
    return g_fw_data;
}

} // namespace notyvos::fs
