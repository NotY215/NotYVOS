# ADR 0001 — Use Limine as initial bootloader

Status: Accepted
Date: 2025

## Context
We need a UEFI bootloader to reach our kernel. Building one from scratch
now would delay kernel work by months.

## Decision
Use Limine v8.x with the Limine protocol. Pin the version in
cmake/limine.cmake. Replace with a custom NOTYVOS bootloader when the OS
is mature.

## Consequences
- Limine does NOT make NOTYVOS Linux.
- Boot menu format is limine.conf.
- Higher-half kernel linked at 0xffffffff80000000.