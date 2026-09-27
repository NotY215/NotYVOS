#pragma once
#include <kernel/fs/file.hpp>
#include <kernel/fs/vnode.hpp>

namespace notyvos::fs
{

void vfs_init();
void vfs_mount_root(VNode* root);
VNode* vfs_root();
VNode* vfs_lookup(const char* path, const char* cwd);

} // namespace notyvos::fs
