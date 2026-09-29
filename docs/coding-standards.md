# Coding Standards

## Languages

- C++20 is the primary kernel language.
- C17 is used for freestanding user programs and libnoty.
- x86-64 Assembly is used only where required by CPU entry, interrupts,
  context switching, user entry, fork entry and JIT entry.

## Kernel rules

- Kernel code is freestanding.
- Exceptions and RTTI are disabled.
- The kernel uses the NOTYVOS heap for dynamic allocation.
- Null-capable pointers must be checked before use.
- User pointers must cross through the uaccess layer.
- Avoid hidden host-runtime dependencies.
- Keep hardware-specific code inside its subsystem or architecture layer.
- Public subsystem interfaces belong under kernel/include/kernel/.
- -Werror remains enabled; warnings should be fixed rather than suppressed.

## Naming and layout

- Headers use .hpp.
- C++ sources use .cpp.
- C sources use .c.
- Assembly sources use .S.
- Namespaces start with notyvos:: and follow subsystem boundaries.
- Keep implementation-specific helpers private to the source file where
  practical.

## Graphics

Graphics code should use the device-independent Graphics API or HAL instead
of directly depending on a future hardware backend. The compositor owns the
desktop scene and coordinates input, windows and presentation.

## PS3 runtime

PS3 runtime code stays under kernel/ps3/. It must not introduce a dependency
from the native boot path on Sony firmware. Translation code must preserve
the PPU context contract and use executable memory obtained through the
NOTYVOS executable arena.

## Documentation

Documentation must distinguish implemented code, runtime-verified behavior,
and planned roadmap work. Do not mark a feature complete only because an
interface or placeholder exists.
