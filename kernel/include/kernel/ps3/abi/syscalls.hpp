#pragma once
#include <kernel/ps3/ppu.hpp>
#include <kernel/types.hpp>

namespace notyvos::ps3::abi
{

namespace nr
{
// Process
constexpr u64 kProcessExit = 1;
constexpr u64 kProcessFork = 2;
constexpr u64 kProcessGetPid = 20;

// File descriptor I/O
constexpr u64 kRead = 3;
constexpr u64 kWrite = 4;
constexpr u64 kOpen = 5;
constexpr u64 kClose = 6;
constexpr u64 kLseek = 7;
constexpr u64 kStat = 8;
constexpr u64 kFstat = 9;

// Cell filesystem
constexpr u64 kCellFsOpen = 202;
constexpr u64 kCellFsRead = 203;
constexpr u64 kCellFsWrite = 204;
constexpr u64 kCellFsClose = 205;
constexpr u64 kCellFsLseek = 206;
constexpr u64 kCellFsStat = 207;
constexpr u64 kCellFsOpendir = 208;
constexpr u64 kCellFsReaddir = 209;
constexpr u64 kCellFsClosedir = 210;
constexpr u64 kCellFsUnlink = 211;
constexpr u64 kCellFsMkdir = 212;
constexpr u64 kCellFsRename = 213;

// Firmware access. Returns (address, size) in two guest-readable slots.
constexpr u64 kFirmwareGet = 300;
} // namespace nr

// Guest-visible stat structure. Mirrors the fields guest code expects.
struct GuestStat
{
    u32 mode;
    u32 uid;
    u32 gid;
    u32 size;
    u64 atime;
    u64 mtime;
    u64 ctime;
};

// Guest-visible dirent. 64-byte name is enough for VFS entries.
struct GuestDirent
{
    char name[256];
    u8 type; // 0 = file, 1 = dir
    u8 _pad[7];
    u64 size;
};

void install(ppu::Context* ctx) noexcept;
bool dispatch(void* user, ppu::Context* ctx) noexcept;

u64 calls_handled() noexcept;
u64 calls_unknown() noexcept;
u64 process_exits() noexcept;

} // namespace notyvos::ps3::abi
