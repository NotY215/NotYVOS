#pragma once

#include <kernel/types.hpp>

namespace notyvos::log {

enum class Level { Debug, Info, Warn, Error };

void init() noexcept;
void set_level(Level lvl) noexcept;
Level level() noexcept;

void write(Level lvl, const char* tag, const char* fmt, ...) noexcept;

} // namespace notyvos::log

#define NOTYVOS_DEBUG(tag, ...) ::notyvos::log::write(::notyvos::log::Level::Debug, tag, __VA_ARGS__)
#define NOTYVOS_INFO(tag, ...)  ::notyvos::log::write(::notyvos::log::Level::Info,  tag, __VA_ARGS__)
#define NOTYVOS_WARN(tag, ...)  ::notyvos::log::write(::notyvos::log::Level::Warn,  tag, __VA_ARGS__)
#define NOTYVOS_ERR(tag, ...)   ::notyvos::log::write(::notyvos::log::Level::Error, tag, __VA_ARGS__)