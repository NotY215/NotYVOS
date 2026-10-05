#pragma once
#include <kernel/block/block.hpp>
#include <kernel/fs/vnode.hpp>

namespace notyvos::fs
{

// Mount NYFS on the given block device. Formats the disk if the superblock
// magic is not present.
VNode* nyfs_mount(block::BlockDevice* dev);

// Returns the mounted block device, or nullptr.
block::BlockDevice* nyfs_device();

// File operations exposed as VNodeOps. Used internally by the VFS layer.
isize nyfs_read(VNode* n, void* buf, usize off, usize len);
isize nyfs_write(VNode* n, const void* buf, usize off, usize len);
isize nyfs_size(VNode* n);

// Create a file. Returns 0 on success, -1 on failure.
int nyfs_create(const char* name);

// Remove a file. Returns 0 on success, -1 on failure.
int nyfs_unlink(const char* name);

// Rename a file in place. Returns 0 on success, -1 on failure.
int nyfs_rename(const char* old_name, const char* new_name);

// Refresh the in-memory VNode tree from disk.
void nyfs_rescan();

} // namespace notyvos::fs
