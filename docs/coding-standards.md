# Coding Standards

- C++23, freestanding, exceptions off, RTTI off.
- Assembly only where required (entry, context, ISR stubs).
- File extension: `.hpp`, `.cpp`, `.S`.
- Namespace: `notyvos::` (and sub-namespaces per subsystem).
- No dynamic allocation outside the kernel heap, once it exists.
- No `new`/`delete` until kernel heap lands in Phase 1E.
- All pointers that could be null must be checked.
- `-Werror` is on; do not silence warnings without an ADR.