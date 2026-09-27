#pragma once
#include <kernel/fs/vnode.hpp>

namespace notyvos::fs
{
VNode* initramfs_mount(const void* image, usize size);
}
