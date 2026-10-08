#pragma once
#include <kernel/types.hpp>

namespace notyvos::img::inflate
{

// Raw DEFLATE (RFC 1951) decompressor. No zlib header parsing -- see
// decompress_zlib() for that.
//
// `out_cap` is the maximum number of bytes to write to `out`. Returns
// the number of bytes actually written, or 0 on failure.
usize decompress_raw(const u8* in, usize in_size, u8* out, usize out_cap) noexcept;

// Wraps decompress_raw() with the 2-byte zlib header and the 4-byte
// Adler32 trailer. Returns bytes written, 0 on failure.
usize decompress_zlib(const u8* in, usize in_size, u8* out, usize out_cap) noexcept;

} // namespace notyvos::img::inflate
