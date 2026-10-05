# NOTYVOS Excalidraw source

Use the Excalidraw canvas to render this architecture sketch.

[UEFI] -> [Limine] -> [Kernel]
                    |
       +------------+-------------+
       |            |             |
   [Memory]     [Syscalls]    [Graphics]
       |            |             |
 [Scheduler]      [VFS]      [Compositor]
                      |             |
                    [NYFS]      [TrueType]
                                    |
                                [Desktop]
                                    |
                                [Explorer]

[Kernel] -> [PS3 Runtime] -> [PPU/JIT] -> [SPU/DMA] -> [RSX]
                         |
                    [GameRunner]

This file intentionally stores the editable source description rather than a generated binary canvas export.