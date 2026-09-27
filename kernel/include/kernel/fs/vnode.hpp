#pragma once
#include <kernel/types.hpp>

namespace notyvos::fs
{

enum class VType : u8
{
    File,
    Dir,
    Device
};

struct VNodeOps;
struct VNode
{
    u64 ino;
    VType type;
    VNodeOps* ops;
    void* priv;
    VNode* parent;
    VNode* children;
    VNode* next;
    char name[64];
};

struct DirEntry
{
    char name[64];
    VType type;
    u64 ino;
    u64 size;
};

struct VNodeOps
{
    isize (*read)(VNode*, void* buf, usize off, usize len);
    isize (*write)(VNode*, const void* buf, usize off, usize len);
    isize (*readdir)(VNode*, usize idx, DirEntry* out);
    VNode* (*lookup)(VNode*, const char* name);
    isize (*size)(VNode*);
};

VNode* vnode_alloc(u64 ino, VType type, const char* name);
void vnode_attach(VNode* parent, VNode* child);
VNode* vnode_find_child(VNode* dir, const char* name);
void vnode_free_recursive(VNode* n);

} // namespace notyvos::fs
