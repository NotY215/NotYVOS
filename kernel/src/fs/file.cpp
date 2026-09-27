#include <kernel/fs/file.hpp>
#include <kernel/libk/mem.hpp>
#include <kernel/mm/heap.hpp>

namespace notyvos::fs
{

FileTable* filetable_create()
{
    auto* ft = static_cast<FileTable*>(mm::Heap::allocate(sizeof(FileTable)));
    if (!ft)
        return nullptr;
    libk::memset(ft, 0, sizeof(FileTable));
    return ft;
}

void filetable_destroy(FileTable* ft)
{
    if (ft)
        mm::Heap::deallocate(ft);
}

i32 filetable_alloc(FileTable* ft, VNode* vnode, u32 flags)
{
    if (!ft || !vnode)
        return -1;
    for (u32 i = 0; i < kMaxFds; ++i)
    {
        if (!ft->fds[i].used)
        {
            ft->fds[i].vnode = vnode;
            ft->fds[i].offset = 0;
            ft->fds[i].flags = flags;
            ft->fds[i].used = true;
            return static_cast<i32>(i);
        }
    }
    return -1;
}

File* filetable_get(FileTable* ft, i32 fd)
{
    if (!ft || fd < 0 || fd >= static_cast<i32>(kMaxFds))
        return nullptr;
    if (!ft->fds[fd].used)
        return nullptr;
    return &ft->fds[fd];
}

void filetable_close(FileTable* ft, i32 fd)
{
    if (!ft || fd < 0 || fd >= static_cast<i32>(kMaxFds))
        return;
    ft->fds[fd].used = false;
    ft->fds[fd].vnode = nullptr;
    ft->fds[fd].offset = 0;
}

} // namespace notyvos::fs
