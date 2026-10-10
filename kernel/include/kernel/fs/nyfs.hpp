#pragma once
#include <kernel/block/block.hpp>
#include <kernel/fs/vnode.hpp>

namespace notyvos::fs
{

constexpr u32 kNyfsBlockSize = 512;
constexpr u32 kNyfsInodeSize = 128;
constexpr u32 kNyfsInodeCount = 256;
constexpr u32 kNyfsMagicSize = 8;
constexpr u32 kNyfsRootInode = 1;
constexpr u32 kNyfsDirEntrySize = 64;
constexpr u32 kNyfsDirectBlocks = 12;

// Permission bits (POSIX-like).
constexpr u16 kPermOwnerR = 0x100;
constexpr u16 kPermOwnerW = 0x080;
constexpr u16 kPermOwnerX = 0x040;
constexpr u16 kPermGroupR = 0x020;
constexpr u16 kPermGroupW = 0x010;
constexpr u16 kPermGroupX = 0x008;
constexpr u16 kPermOtherR = 0x004;
constexpr u16 kPermOtherW = 0x002;
constexpr u16 kPermOtherX = 0x001;
constexpr u16 kPermAll = 0x1FF;
constexpr u16 kPermDefaultFile =
    kPermOwnerR | kPermOwnerW | kPermGroupR | kPermGroupW | kPermOtherR | kPermOtherW;
constexpr u16 kPermDefaultDir = kPermDefaultFile | kPermOwnerX | kPermGroupX | kPermOtherX;

enum class NyfsInodeMode : u16
{
    Free = 0,
    File = 1,
    Dir = 2,
};

struct NyfsSuperblock
{
    char magic[kNyfsMagicSize];
    u32 block_size;
    u32 total_blocks;
    u32 inode_count;
    u32 inode_bitmap_lba;
    u32 block_bitmap_lba;
    u32 block_bitmap_blocks;
    u32 inode_table_lba;
    u32 inode_table_blocks;
    u32 data_start_lba;
    u32 data_blocks;
    u32 root_inode;
    u32 free_blocks;
    u32 free_inodes;
    u32 journal_start_lba;
    u32 journal_blocks;
    u32 journal_head;
    u8 pad[kNyfsBlockSize - 8 - 16 * 4];
};

static_assert(sizeof(NyfsSuperblock) == kNyfsBlockSize, "NYFS superblock must fit one block");

struct NyfsInode
{
    u16 mode;
    u16 links;
    u32 size;
    u32 uid;
    u32 gid;
    u32 atime;
    u32 mtime;
    u32 ctime;
    u32 blocks[kNyfsDirectBlocks];
    u32 indirect;
    u16 perms;
    u16 reserved0;
    u32 reserved[11];
};

static_assert(sizeof(NyfsInode) == kNyfsInodeSize, "NYFS inode must be 128 bytes");

struct NyfsDirEntry
{
    u32 inode;
    u8 type;
    u8 name_len;
    u16 reserved;
    char name[56];
};

static_assert(sizeof(NyfsDirEntry) == kNyfsDirEntrySize, "NYFS dir entry must be 64 bytes");

VNode* nyfs_mount(block::BlockDevice* dev);
void nyfs_rescan();
block::BlockDevice* nyfs_device();

isize nyfs_read(VNode* n, void* buf, usize off, usize len);
isize nyfs_write(VNode* n, const void* buf, usize off, usize len);
isize nyfs_size(VNode* n);

int nyfs_create(const char* name);
int nyfs_unlink(const char* name);
int nyfs_rename(const char* old_name, const char* new_name);

VNode* nyfs_create_in(VNode* parent, const char* name, VType type);
int nyfs_mkdir(VNode* parent, const char* name);
int nyfs_rmdir(VNode* parent, const char* name);
int nyfs_truncate(VNode* node, u32 new_size);
int nyfs_chmod(VNode* node, u16 mode);

// Returns true if `n` is a NYFS-managed VNode (safe to pass to the
// functions above).
bool nyfs_owns_vnode(const VNode* n) noexcept;

// Journal.
void nyfs_journal_init() noexcept;
void nyfs_journal_flush() noexcept;
void nyfs_journal_recover() noexcept;
void nyfs_journal_log_write(u32 lba, const void* data) noexcept;

// fsck.
bool nyfs_fsck(bool repair) noexcept;

// Statistics.
struct NyfsStats
{
    u32 total_blocks;
    u32 free_blocks;
    u32 total_inodes;
    u32 free_inodes;
    u32 block_size;
};
NyfsStats nyfs_stats();

} // namespace notyvos::fs
