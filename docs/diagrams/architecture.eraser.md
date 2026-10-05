# NOTYVOS Eraser source

Paste the fenced Eraser diagram into [Eraser.io](https://eraser.io) /
DiagramGPT, then reconcile generated output against `docs/architecture.md`
and `docs/roadmap.md` before committing any regenerated canvas.

```eraser
title NOTYVOS architecture
direction right

Boot {
  UEFI [icon: server]
  Limine [icon: server]
}
UEFI > Limine: boot

Kernel {
  CPU [icon: cpu]
  Memory [icon: storage]
  Scheduler [icon: workflow]
  Syscalls [icon: api-gateway]
  VFS [icon: folder]
  NYFS [icon: database]
}
Limine > CPU: kernel entry
CPU > Memory
Memory > Scheduler
Scheduler > Syscalls
Syscalls > VFS
VFS > NYFS: /disk

Desktop {
  Graphics [icon: monitor]
  Compositor [icon: layout]
  TrueType [icon: file]
  Explorer [icon: folder]
}
Syscalls > Graphics
TrueType > Compositor: font::draw_text()
Graphics > Compositor
Compositor > Explorer
Explorer > VFS

PS3 {
  GameRunner [icon: play]
  PPU [icon: cpu]
  JIT [icon: code]
  SPU [icon: cpu]
  RSX [icon: monitor]
}
CPU > GameRunner
GameRunner > PPU
PPU > JIT
PPU > SPU
PPU > RSX
```
