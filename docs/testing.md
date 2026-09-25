# Testing

Phase 0 test = boot smoke test:

1. Build ISO.
2. Launch QEMU with ISO.
3. Verify serial output contains:
     NOTYVOS kernel alive
     [INF] boot: NOTYVOS 0.1.0 (<rev>)
     [INF] fb: framebuffer <W>x<H> pitch=<P> bpp=<B>
     [INF] mm: memory map entries: <N>
     [INF] mm: HHDM offset: 0x...
     [INF] boot: Phase 0 boot OK. Halting.
4. Verify framebuffer shows NOTYVOS / kernel alive / Phase 0 boot OK.