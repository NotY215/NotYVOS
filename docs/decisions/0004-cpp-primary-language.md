# ADR 0004 — C++ primary, C secondary, ASM only where required

Status: Accepted

## Decision
- C++ (freestanding, C++23 where supported) is the primary language.
- C only where technically required.
- x86-64 Assembly only where required.
- No Rust, no C#, no Python/Java as core runtime dependencies.