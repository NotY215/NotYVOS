# ADR 0001 -- Use Limine as the bootloader

Status: Accepted

## Decision

NOTYVOS uses the Limine protocol and pins Limine to v12.9.0 in the CMake
configuration.

The current ISO contains Limine UEFI boot files and the NOTYVOS kernel. The
repository keeps the bootloader integration small so kernel development is
not coupled to a custom bootloader implementation.

## Consequences

- Limine provides the initial boot environment and boot-time information.
- The kernel is linked at the NOTYVOS higher-half address.
- limine.conf remains the boot configuration source.
- A future custom bootloader remains possible, but it is not part of the
  current roadmap.
