#include <kernel/fs/vfs.hpp>
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

} // namespace notyvos::fs
