#pragma once
#include <kernel/types.hpp>

namespace notyvos::audio
{

bool hda_init() noexcept;
bool hda_present() noexcept;
u32 hda_codec_count() noexcept;

} // namespace notyvos::audio
