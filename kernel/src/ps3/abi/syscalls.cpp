#include <kernel/libk/mem.hpp>
#include <kernel/log.hpp>
#include <kernel/ps3/abi/syscalls.hpp>

namespace notyvos::ps3::abi
{

namespace
{
u64 g_handled = 0;
u64 g_unknown = 0;
u64 g_exits   = 0;

// PPU GPR indices used by the CellOS ABI.
constexpr u32 kSysnoReg = 3;
constexpr u32 kArg0Reg  = 4;
constexpr u32 kArg1Reg  = 5;
constexpr u32 kArg2Reg  = 6;
constexpr u32 kRetReg   = 3;
} // namespace

void install(ppu::Context* ctx) noexcept
{
    if (!ctx) return;
    ctx->syscall = &dispatch;
    ctx->user    = ctx;
}

bool dispatch(void* user, ppu::Context* ctx) noexcept
{
    (void)user;
    const u64 nr  = ctx->gpr[kSysnoReg];
    const u64 a0  = ctx->gpr[kArg0Reg];
    const u64 a1  = ctx->gpr[kArg1Reg];
    const u64 a2  = ctx->gpr[kArg2Reg];
    (void)a1; (void)a2;

    switch (nr)
    {
    case nr::kProcessExit:
        ++g_exits;
        log::write(log::Level::Info, "ps3-abi",
            "sys_process_exit(%llu)", static_cast<unsigned long long>(a0));
        ctx->gpr[kRetReg] = 0;
        // Signal the caller to stop by returning false after writing GPRs.
        // The PPU runtime uses this to break the loop.
        return false;

    case nr::kProcessFork:
        log::write(log::Level::Info, "ps3-abi", "sys_process_fork");
        ctx->gpr[kRetReg] = 1;
        ++g_handled;
        return true;

    case nr::kRead:
    case nr::kWrite:
        // Phase 7C will route these through the VFS.
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;

    case nr::kOpen:
    case nr::kClose:
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;

    case nr::kGetPid:
        ctx->gpr[kRetReg] = 1;  // single guest process for now
        ++g_handled;
        return true;

    case nr::kCellFsOpen:
    case nr::kCellFsRead:
    case nr::kCellFsWrite:
    case nr::kCellFsClose:
        // Stubs; real filesystem bridge in 7C.
        ctx->gpr[kRetReg] = 0;
        ++g_handled;
        return true;

    default:
        ++g_unknown;
        log::write(log::Level::Warn, "ps3-abi",
            "unhandled syscall %llu (a0=0x%llx)",
            static_cast<unsigned long long>(nr),
            static_cast<unsigned long long>(a0));
        ctx->gpr[kRetReg] = static_cast<u64>(-1);
        return true;
    }
}

u64 calls_handled()  noexcept { return g_handled; }
u64 calls_unknown()  noexcept { return g_unknown; }
u64 process_exits()  noexcept { return g_exits; }

} // namespace notyvos::ps3::abi