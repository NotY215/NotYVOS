#pragma once
#include <kernel/fs/file.hpp>
#include <kernel/fs/vnode.hpp>

namespace notyvos::fs
{

void vfs_init();
void vfs_mount_root(VNode* root);
VNode* vfs_root();
VNode* vfs_lookup(const char* path, const char* cwd);
// Directory operations. Currently backed by NYFS when the parent is the
// mounted disk root; other filesystems are read-only.
// Return 0 on success, -1 on failure.
int vfs_create(VNode* parent, const char* name) noexcept;
int vfs_unlink(VNode* node) noexcept;
int vfs_rename(VNode* node, const char* new_name) noexcept;
// Register a raw firmware blob. Called from main.cpp once the Limine
// modules are known. GameRunner queries it via vfs_firmware().
void vfs_register_firmware(const void* data, usize size) noexcept;
const void* vfs_firmware(usize* out_size) noexcept;

} // namespace notyvos::fs
