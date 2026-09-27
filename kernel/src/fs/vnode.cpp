#include <kernel/fs/vnode.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/libk/string.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::fs
{

namespace
{
u64 g_next_ino = 1;
}

VNode* vnode_alloc(u64 ino, VType type, const char* name)
{
    auto* n = static_cast<VNode*>(mm::Heap::allocate(sizeof(VNode)));
    if (!n)
        return nullptr;
    libk::memset(n, 0, sizeof(VNode));
    n->ino = ino ? ino : g_next_ino++;
    n->type = type;
    libk::strncpy(n->name, name ? name : "", sizeof(n->name) - 1);
    return n;
}

void vnode_attach(VNode* parent, VNode* child)
{
    if (!parent || !child)
        return;
    child->parent = parent;
    child->next = parent->children;
    parent->children = child;
}

VNode* vnode_find_child(VNode* dir, const char* name)
{
    if (!dir || dir->type != VType::Dir)
        return nullptr;
    for (VNode* c = dir->children; c; c = c->next)
        if (libk::strcmp(c->name, name) == 0)
            return c;
    return nullptr;
}

void vnode_free_recursive(VNode* n)
{
    if (!n)
        return;
    VNode* c = n->children;
    while (c)
    {
        VNode* nx = c->next;
        vnode_free_recursive(c);
        c = nx;
    }
    mm::Heap::deallocate(n);
}

} // namespace notyvos::fs
