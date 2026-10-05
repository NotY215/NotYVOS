# NOTYVOS Excalidraw source

Open [`architecture.excalidraw.json`](architecture.excalidraw.json) in
Excalidraw (File → Open) or any `@excalidraw/excalidraw` host. The JSON is
the editable canvas. This file is the same topology in sketch form.

Color key: blue = boot, orange = kernel, green = desktop, purple = PS3.

```text
[UEFI] -> [Limine] -> [Kernel]
                         |
        +----------------+------------------+
        |                |                  |
    [Memory]         [Syscalls]         [Graphics]
                         |                  |
                       [VFS]           [TrueType]
                         |                  |
                     [NYFS]           [Compositor]
                                            |
                                        [Desktop]
                                            |
                                       [Explorer]

[GameRunner] -> [PS3 Runtime] -> [PPU/JIT] -> [SPU/DMA] -> [RSX]
```

TrueType sits on the desktop path and feeds the compositor through
`font::draw_text()`. Explorer file operations return to VFS / NYFS.
