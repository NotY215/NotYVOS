#pragma once
#include <kernel/types.hpp>

namespace notyvos::gfx::clipboard
{

// ---------------------------------------------------------------------------
// Desktop-wide clipboard.
//
// One slot at a time. Text or a single file path. The Explorer places
// file paths with a "cut" flag; the Terminal places plain text; any app
// can read via get().
// ---------------------------------------------------------------------------

enum class Kind : u8
{
    Empty = 0,
    Text  = 1,   // UTF-8 text
    File  = 2,   // absolute path to a file in the VFS
};

constexpr usize kMaxClipText = 1024;

struct Content
{
    Kind kind;
    char text[kMaxClipText];
    bool cut;         // meaningful only for Kind::File
};

// Store UTF-8 text. Truncates to kMaxClipText.
void set_text(const char* text) noexcept;

// Store a file path. `cut` distinguishes Ctrl+X from Ctrl+C.
void set_file(const char* path, bool cut) noexcept;

// Read the current clipboard. Never null.
const Content& get() noexcept;

// True if anything is stored.
bool has() noexcept;

// Clear.
void clear() noexcept;

// Statistics.
u64 writes() noexcept;

} // namespace notyvos::gfx::clipboard