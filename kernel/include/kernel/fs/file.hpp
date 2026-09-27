#pragma once
#include <kernel/fs/vnode.hpp>

namespace notyvos::fs
{

constexpr u32 kMaxFds = 64;

struct File
{
    VNode* vnode;
    usize offset;
    u32 flags;
    bool used;
};

struct FileTable
{
    File fds[kMaxFds];
};

FileTable* filetable_create();
void filetable_destroy(FileTable* ft);
i32 filetable_alloc(FileTable* ft, VNode* vnode, u32 flags);
File* filetable_get(FileTable* ft, i32 fd);
void filetable_close(FileTable* ft, i32 fd);

} // namespace notyvos::fs
