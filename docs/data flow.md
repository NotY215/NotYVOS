# NOTYVOS Data Flow

This document maps the data and control flow of the **implemented and
currently active NOTYVOS phases**. It is intentionally based on the current
repository implementation rather than future interfaces.

> **Diagram note:** GitHub renders the Mermaid flow and sequence diagrams
> below. Editable architecture sources (Markmap, D2, Ilograph, Excalidraw,
> Cytoscape.js, GoJS, Eraser, Python Diagrams) live in
> [`docs/diagrams/`](diagrams/README.md). Sequence diagrams show time
> progression from input to output. They do not claim hardware acceleration
> or runtime behavior that has not been implemented.
>
> Mermaid labels avoid `::` and `()`. The C++ entry point is
> `font::draw_text()`; diagrams use the node id `draw_text`.

## System-wide flow

```mermaid
flowchart LR
    FW[UEFI firmware] --> LIM[Limine v12.9.0]
    LIM --> K[Kernel entry]
    K --> CPU[CPU + memory]
    CPU --> PROC[Processes + scheduler]
    CPU --> VFS[VFS]
    VFS --> RAMFS[Initramfs /]
    VFS --> NYFS[NYFS /disk]
    PROC --> SYSCALL[Syscall ABI]
    SYSCALL --> VFS
    SYSCALL --> PROC
    PROC --> INIT[init.elf + shell]
    K --> GFX[Graphics API + HAL]
    GFX --> COMP[Compositor]
    COMP --> DESKTOP[Desktop + windows + widgets]
    DESKTOP --> INPUT[PS/2 keyboard + mouse]
    INPUT --> COMP
    K --> PS3[PS3 runtime]
    PS3 --> DEC[PPC decoder]
    DEC --> PPU[PPU interpreter]
    DEC --> JIT[Baseline x86-64 JIT]
    PS3 --> SPU[SPU interpreter]
    PS3 --> DMA[DMA engine]
    JIT --> PPU
    JIT --> EXEC[Executable arena]
```

## Phase 1 — Kernel Core

### 1A — CPU initialization, GDT, TSS, serial and framebuffer

```mermaid
sequenceDiagram
    participant Boot as Limine
    participant Entry as _start
    participant CPU as CPU setup
    participant GDT as GDT/TSS
    participant IO as Serial/Framebuffer

    Boot->>Entry: Kernel entry + boot information
    Entry->>CPU: Establish execution environment
    CPU->>GDT: Load GDT and TSS
    GDT-->>CPU: Valid kernel CPU state
    CPU->>IO: Initialize diagnostic/output paths
    IO-->>Entry: Kernel can log and present output
```

### 1B — IDT, ISR stubs, exceptions, PIC and PIT

```mermaid
flowchart LR
    EVENT[CPU exception / hardware IRQ] --> IDT[IDT]
    IDT --> ISR[ISR stub]
    ISR --> HANDLER[Kernel interrupt handler]
    PIC[PIC] --> ISR
    PIT[PIT timer] --> PIC
    HANDLER --> RESUME[Return to interrupted context]
```

### 1C — Physical memory manager

```mermaid
flowchart LR
    MAP[Limine memory map] --> PMM[Physical memory manager]
    PMM --> FRAMES[Free frame tracking]
    FRAMES --> ALLOC[Physical frame allocation]
    ALLOC --> PAGE[Page-table / heap consumers]
```

### 1D — Virtual memory and HHDM

```mermaid
flowchart LR
    PHYS[Physical frames] --> PT[Page tables]
    HHDM[HHDM mapping] --> PT
    PT --> VIRT[Virtual address space]
    VIRT --> KERNEL[Higher-half kernel]
    VIRT --> USER[User mappings]
```

### 1E — Kernel heap

```mermaid
flowchart LR
    VM[Virtual memory] --> HEAP[Kernel heap]
    HEAP --> NEW[new / allocation]
    HEAP --> SUBSYS[Kernel subsystems]
    SUBSYS --> FREE[Free / release]
    FREE --> HEAP
```

### 1F — Logging, panic and assertions

```mermaid
flowchart LR
    CODE[Kernel subsystem] --> LOG[Logger]
    LOG --> SERIAL[Serial console]
    LOG --> FB[Framebuffer console]
    FAIL[Assertion / fatal error] --> PANIC[Panic handler]
    PANIC --> LOG
```

### 1G — Per-CPU state, LAPIC and SMP bring-up

```mermaid
flowchart LR
    BOOT[Bootstrap CPU] --> CPU0[Per-CPU state]
    CPU0 --> LAPIC[LAPIC]
    CPU0 --> SMP[SMP bring-up]
    SMP --> CPU1[Additional CPU state]
    CPU1 --> LAPIC
```

## Phase 2 — Processes, Syscalls, VFS and Userland

### 2A — Ring 3 transition

```mermaid
sequenceDiagram
    participant K as Kernel
    participant U as Ring 3 process
    participant S as syscall entry

    K->>U: Build user stack + entry state
    K->>U: Enter user mode
    U->>S: syscall
    S->>K: Kernel syscall dispatcher
    K-->>S: Return value
    S-->>U: sysret
```

### 2B — Scheduler + threads

```mermaid
flowchart LR
    TASKS[Runnable tasks] --> SCHED[Scheduler]
    TIMER[Timer / preemption] --> SCHED
    SCHED --> SAVE[Context save]
    SAVE --> SWITCH[Context switch]
    SWITCH --> NEXT[Next task]
    NEXT --> RUN[CPU execution]
```

### 2C — Processes + ELF loader

```mermaid
flowchart LR
    ELF[ELF64 program] --> PARSE[ELF validation]
    PARSE --> LOAD[PT_LOAD segments]
    LOAD --> PML4[Per-process PML4]
    PML4 --> STACK[User stack]
    STACK --> ENTRY[Entry point]
    ENTRY --> RUN[User process]
```

### 2C-followup — fork / wait

```mermaid
sequenceDiagram
    participant P as Parent
    participant K as Kernel
    participant C as Child

    P->>K: fork()
    K->>K: Clone user mappings
    K->>C: Create child process
    K-->>P: Child PID
    C-->>P: Child execution continues
    P->>K: wait()
    C->>K: exit / termination
    K->>K: Reap child state
    K-->>P: Child status
```

### 2D — Syscall ABI + uaccess

```mermaid
flowchart LR
    USER[User pointer / syscall args] --> ABI[Syscall ABI]
    ABI --> DISP[Syscall dispatcher]
    DISP --> UACCESS[copy_from_user / copy_to_user]
    UACCESS --> KERNEL[Kernel subsystem]
    KERNEL --> UACCESS
    UACCESS --> RET[Validated return to user]
```

### 2D-followup — readdir / mmap

```mermaid
flowchart LR
    APP[User program] --> SC[Syscall]
    SC --> RD[readdir]
    RD --> VFS[VFS directory]
    VFS --> DENTS[Directory entries]
    SC --> MM[mmap]
    MM --> VM[Virtual memory manager]
    VM --> MAP[Anonymous user mapping]
    DENTS --> APP
    MAP --> APP
```

### 2E — VFS

```mermaid
flowchart LR
    PATH[User path] --> LOOKUP[VFS path lookup]
    LOOKUP --> VNODE[VNode]
    VNODE --> FILE[File]
    FILE --> TABLE[Per-task FileTable]
    TABLE --> FD[File descriptor]
    FD --> READWRITE[Read / write / seek]
```

### 2F — Initramfs

```mermaid
flowchart LR
    LIMINE[Limine initramfs module] --> USTAR[ustar parser]
    USTAR --> ROOT[In-memory VFS root /]
    ROOT --> FILES[hello.txt / readme.txt / hello.elf]
    FILES --> USER[User processes]
```

### 2G — libc + shell

```mermaid
sequenceDiagram
    participant KB as PS/2 keyboard
    participant SH as Shell
    participant LIB as libnoty
    participant SC as Syscalls
    participant K as Kernel

    KB->>SH: Command input
    SH->>LIB: Parse / format
    LIB->>SC: User syscall
    SC->>K: Dispatch
    K-->>SC: Result
    SC-->>LIB: Return value
    LIB-->>SH: Formatted output
    SH-->>KB: Prompt / output path
```

### 2I — exec / brk

```mermaid
flowchart LR
    PROC[Current process] --> EXEC[exec]
    EXEC --> ELF[ELF loader]
    ELF --> NEWADDR[Replacement address space]
    NEWADDR --> ENTRY[New program entry]

    PROC --> BRK[brk]
    BRK --> HEAP[User heap end]
    HEAP --> PAGES[Mapped heap pages]
```

### 2K — Persistent FS (NYFS)

```mermaid
flowchart LR
    FD[User file descriptor] --> VFS[VFS]
    VFS --> NYFS[NYFS]
    NYFS --> SB[512-byte superblock]
    NYFS --> FT[64-entry file table]
    NYFS --> DATA[File data sectors]
    DATA --> DISK[Persistent block device]
    DISK --> NYFS
```

### 2L — Time / sleep / kill

```mermaid
sequenceDiagram
    participant U as User process
    participant K as Kernel
    participant T as Timer/scheduler

    U->>K: time()
    K-->>U: Uptime
    U->>K: sleep(duration)
    K->>T: Block task until deadline
    T->>K: Deadline reached
    K-->>U: Wake task
    U->>K: kill(target, SIGTERM)
    K->>K: Mark target for termination
    K-->>U: Return status
```

### 2M — stdio + malloc

```mermaid
flowchart LR
    APP[User program] --> STDIO[libnoty stdio]
    STDIO --> FD[File descriptor syscalls]
    FD --> VFS[VFS / NYFS]
    APP --> MALLOC[malloc]
    MALLOC --> HEAP[User heap]
    HEAP --> BRK[brk / memory mapping]
```

### 2N — Signals + Ctrl+C

```mermaid
sequenceDiagram
    participant KB as PS/2 keyboard
    participant SH as Shell
    participant K as Kernel
    participant T as Target task

    KB->>SH: Ctrl+C
    SH->>K: SIGINT delivery path
    K->>T: Deliver SIGINT
    T->>K: Termination handling
    K-->>SH: Scheduler continues
```

## Phase 3 — Desktop, Drivers and Graphics

### 3A — Compositor + mouse

```mermaid
flowchart LR
    MOUSE[PS/2 mouse packet] --> INPUT[Input handling]
    INPUT --> COMP[Compositor]
    COMP --> SCENE[Desktop scene]
    SCENE --> BACK[Back buffer]
    BACK --> FB[Framebuffer/VBE]
    FB --> SCREEN[Display]
```

### 3B — Window manager

```mermaid
flowchart LR
    INPUT[Mouse / keyboard] --> WM[Window manager]
    WM --> FOCUS[Focused window]
    WM --> DRAG[Drag state]
    WM --> CTRL[Min/max/close]
    FOCUS --> COMP[Compositor]
    DRAG --> COMP
    CTRL --> COMP
```

### 3C — Shell + power menu

```mermaid
flowchart LR
    START[Start button] --> MENU[Start menu]
    MENU --> LAUNCH[Launcher]
    MENU --> POWER[Power actions]
    LAUNCH --> APPS[Desktop applications]
    POWER --> ACPI[ACPI power/restart]
```

### 3D — Native drivers + widgets

```mermaid
flowchart LR
    ACPI[ACPI] --> POWER[Power control]
    AHCI[AHCI] --> STORAGE[Block storage]
    E1000[e1000] --> NET[Network device]
    HDA[HDA] --> AUDIO[Audio device]
    PS2[PS/2] --> INPUT[Keyboard + mouse]
    WIDGET[Button / Label / ListView] --> GFX[Graphics API]
    INPUT --> DESKTOP[Desktop]
    STORAGE --> VFS[VFS]
```

### 3E — Graphics API + HAL

```mermaid
flowchart LR
    APPGFX[Desktop / graphics caller] --> API[Graphics API]
    API --> HAL[Graphics HAL]
    HAL --> SW[Software backend]
    HAL --> VBE[VBE backend]
    SW --> RASTER[Software rasterization]
    VBE --> FB[Framebuffer presentation]
    RASTER --> FB
    FB --> DISPLAY[Display]
```

## Phase 4 — PS3 Runtime Foundation

### 4A — PS3 loader + PPC decoder

```mermaid
flowchart LR
    PELF[PS3 ELF64] --> LOADER[PS3 ELF loader]
    LOADER --> SEG[Loadable segments]
    SEG --> PPC[PowerPC code]
    PPC --> DEC[PowerPC decoder]
    DEC --> INST[Decoded instruction]
```

### 4B — PPU interpreter

```mermaid
flowchart LR
    INST[Decoded PPU instruction] --> PPU[PPU interpreter]
    PPU --> GPR[GPR / CR / LR / CTR / XER]
    PPU --> FPR[FPR state]
    PPU --> MEM[PPU memory callbacks]
    GPR --> NEXT[Next PC]
    FPR --> NEXT
    MEM --> NEXT
```

### 4C — SPU interpreter

```mermaid
flowchart LR
    SPU_CODE[SPU instruction] --> SPU[SPU interpreter]
    SPU --> REG[SPU registers]
    SPU --> LS[256 KiB local store]
    SPU --> MB[Mailboxes]
    MB --> HOST[PPU/runtime interaction]
```

### 4D — DMA engine + sync

```mermaid
flowchart LR
    PPU_MEM[Main memory] --> DMA[Cell-style DMA queue]
    DMA --> LS[SPU local store]
    LS --> DMA
    DMA --> TAG[DMA tag state]
    TAG --> SYNC[Wait / barrier / synchronization]
```

### 4E — Executable arena + emitter + translation-cache infrastructure

```mermaid
flowchart LR
    PPC[PPU basic block] --> CACHE[Translation cache lookup]
    CACHE -->|miss| EMIT[x86-64 emitter]
    EMIT --> ARENA[Executable memory arena]
    ARENA --> BLOCK[Translated block]
    BLOCK --> CACHE
    CACHE -->|hit| BLOCK
    BLOCK --> TRAMP[JIT trampoline]
    TRAMP --> CPU[x86-64 execution]
```

## Phase 5 — Native Translation

All Phase 5 subphases are delivered: **5A, 5B, 5C and 5D**.

### 5A — Baseline JIT + trampoline + self-test

**Status: DONE.**

```mermaid
sequenceDiagram
    participant P as PPU context
    participant J as JIT
    participant C as Translation cache
    participant E as x86-64 emitter
    participant A as Executable arena
    participant T as Trampoline
    participant X as x86-64 CPU

    P->>J: Execute current PPC PC
    J->>C: Lookup translated block
    alt Cache miss
        C-->>J: No block
        J->>E: Translate supported block
        E->>A: Emit executable bytes
        A-->>J: Executable block
        J->>C: Store translation
    else Cache hit
        C-->>J: Existing block
    end
    J->>T: Enter translated block
    T->>X: Execute native x86-64
    X-->>P: Update PPU context / continue
```

### 5B — Memory opcodes + conditional branch + block chaining

**Status: DONE.**

Implemented data flow:

```mermaid
flowchart LR
    PPC[PPC block] --> DECODE[Decode]
    DECODE --> MEMOPS[Memory translation]
    DECODE --> BRANCH[Conditional branch translation]
    BRANCH --> TARGET[Target block]
    TARGET --> CHAIN[Block chaining]
    MEMOPS --> NATIVE[x86-64 memory operations]
    NATIVE --> CACHE[Translation cache]
    CHAIN --> CACHE
```

The translated memory and control-flow path feeds the completed translation cache and native execution path.

### 5C — FPU + VMX translation

```mermaid
flowchart LR
    PPCF[PPC FPU / VMX instruction] --> DEC[Decoder]
    DEC --> FPU[FPU translation]
    DEC --> VMX[VMX/vector translation]
    FPU --> X86F[x86-64 floating-point state]
    VMX --> X86V[x86-64 vector state]
    X86F --> CTX[PPU context]
    X86V --> CTX
```

**Status: DONE.**

### 5D — Cache invalidation + self-modifying-code detection

```mermaid
flowchart LR
    WRITE[Guest code write] --> DETECT[Code modification detection]
    DETECT --> INVALID[Invalidate affected translation]
    INVALID --> CACHE[Translation cache]
    CACHE --> RETRANS[Re-translate block]
    RETRANS --> EXEC[Executable arena]
```

**Status: DONE.**

## Phase 6 — RSX Graphics Compatibility

The completed RSX path covers 6A–6F: command processing, rasterization,
buffers, depth/scissor, smooth shading, texture binding, UVs, wrapping,
perspective correction and mipmap/LOD sampling.

```mermaid
flowchart LR
    PPU[PPU / JIT] --> FIFO[RSX FIFO]
    FIFO --> METH[Method decoder]
    METH --> RASTER[Software rasterizer]
    METH --> TEX[Texture bind]
    TEX --> UV[UVs + wrap + perspective]
    UV --> MIP[Mipmap / LOD sample]
    MIP --> RASTER
    RASTER --> DEPTH[Depth + scissor]
    RASTER --> SHADE[Smooth shading]
    DEPTH --> COLOR[Color buffer]
    SHADE --> COLOR
    COLOR --> FB[Framebuffer]
```

```d2
direction: right
PPU: PPU / JIT
FIFO: RSX FIFO
Decode: Method decoder
Raster: Software rasterizer
Tex: Texture + UV + mip
FB: Framebuffer

PPU -> FIFO -> Decode
Decode -> Raster
Decode -> Tex -> Raster
Raster -> FB
```

## Phase 7 — GameRunner + Compatibility Layer

The completed GameRunner path is documented through 7C. Phase 7D remains
the next runtime integration stage.

```mermaid
flowchart LR
    FILE[Guest ELF / package] --> DETECT[Format detection]
    DETECT --> LOAD[PS3 ELF loader]
    LOAD --> ABI[PS3 ABI / syscalls]
    ABI --> PPU[PPU interpreter / JIT]
    ABI --> FS[cellFs]
    FS --> VFS[VFS]
    VFS --> NYFS[NYFS]
    PPU --> RSX[RSX path]
```

## Phase 8 — Rendering Validation

The completed 8A/8B validation path exercises the RSX rendering pipeline
through framebuffer output.

```mermaid
flowchart LR
    SUITE[Validation suite] --> RSX[RSX pipeline]
    RSX --> FB[Framebuffer]
    FB --> CHECK[Reference compare]
    CHECK --> LOG[Self-test log]
```

## Phase 9 — Image Support

The completed image pipeline through 9A–9C: BMP, PNG/inflate, GIF/LZW, ICO
and JPEG decoding.

```mermaid
flowchart LR
    PATH[VFS file] --> KIND{Format}
    KIND --> BMP[BMP]
    KIND --> PNG[PNG / inflate]
    KIND --> GIF[GIF / LZW]
    KIND --> ICO[ICO]
    KIND --> JPEG[JPEG]
    BMP --> PIX[Decoded pixel buffer]
    PNG --> PIX
    GIF --> PIX
    ICO --> PIX
    JPEG --> PIX
    PIX --> VIEW[Image Viewer]
    VIEW --> GFX[Graphics API]
    GFX --> COMP[Compositor]
```

## Phase 10 — Theme and UI Management

The completed theme and Settings path through 10A–10F.

```mermaid
flowchart LR
    SET[Settings Appearance] --> THEME[Theme state]
    THEME --> DARK[Dark]
    THEME --> LIGHT[Light]
    THEME --> MAC[macOS Dark]
    SHORT[Desktop shortcuts] --> THEME
    THEME --> STYLE[Desktop styling]
    STYLE --> COMP[Compositor]
    COMP --> FB[Framebuffer]
```

## Phase 11 — TrueType Font Subsystem

**Status: DONE.** Multiple weights, kerning, complex-script shaping and
subpixel horizontal rendering remain deferred.

The compositor entry point is `font::draw_text()`. Mermaid nodes use
`draw_text` because `::` and `()` are Mermaid class/stadium tokens and quoted
labels still fail type detection in some renderers.

```mermaid
flowchart LR
    FONT[Inter TTF] --> PARSE[TrueType table parser]
    PARSE --> HEAD[head hhea hmtx maxp]
    PARSE --> CMAP[cmap 4 and 12]
    PARSE --> LOCA[loca and glyf]
    HEAD --> GLYPH[Simple glyph outline]
    CMAP --> GLYPH
    LOCA --> GLYPH
    GLYPH --> FLAT[Quadratic Bezier flattening]
    FLAT --> RASTER[4x coverage rasterizer]
    RASTER --> CACHE[Per-face glyph cache]
    CACHE --> DRAW[draw_text]
    DRAW --> COMP[Compositor]
    COMP --> FB[Framebuffer]
```

```mermaid
sequenceDiagram
    participant Face as Inter Regular
    participant Parse as TTF parser
    participant Cache as Glyph cache
    participant Draw as draw_text
    participant Comp as Compositor
    participant FB as Framebuffer

    Face->>Parse: Load required tables
    Parse->>Parse: Extract simple outline
    Parse->>Parse: Flatten quadratic Beziers
    Parse->>Cache: Rasterize 4x coverage
    Draw->>Cache: Lookup codepoint
    alt Cache miss
        Cache->>Cache: Rasterize and insert
    end
    Cache-->>Draw: Cached glyph coverage
    Draw->>Comp: Blit anti-aliased run
    Comp->>FB: Present desktop scene
```

```d2
direction: right
Inter: Inter Regular TTF {shape: page}
Parser: TrueType table parser
Outline: Simple glyph outline
Flat: Quadratic Bezier flatten
Raster: 4x coverage rasterizer
Cache: Per-face glyph cache {shape: cylinder}
Draw: "font::draw_text()"
Compositor: Compositor
FB: Framebuffer {shape: rectangle}

Inter -> Parser -> Outline -> Flat -> Raster -> Cache -> Draw -> Compositor -> FB
```

### Phase 11 map (Markmap source)

- Phase 11 TrueType
  - Parser
    - head, hhea, hmtx, maxp
    - cmap formats 4 and 12
    - loca + glyf
  - Outline
    - Simple glyphs
    - Quadratic Bezier flattening
  - Rasterizer
    - 4x vertical supersampling
    - Coverage AA
  - Cache
    - 512-entry per-face
    - Age-based eviction
  - Integration
    - font::draw_text()
    - Compositor
    - Boot self-test
  - Deferred
    - Multiple weights
    - Kerning
    - Complex-script shaping
    - Subpixel horizontal rendering


# Phase 12 — Explorer 10G + Real File Operations

```mermaid
flowchart LR
    INPUT[Mouse / keyboard] --> EXP[Explorer]
    EXP --> VIEW[Grid or details view]
    EXP --> BREAD[Clickable breadcrumb]
    EXP --> OPS[Real VFS operations]
    OPS --> VFS[VFS]
    VFS --> NYFS[NYFS /disk]
    NYFS --> DISK[Persistent storage]
```

**Status: NEXT.** Delivery order: 12A grid/details, 12B breadcrumb, 12C real Open/New/Rename/Delete/Properties operations.

# Queued roadmap flows

```mermaid
flowchart TD
    P12[12 Explorer + file operations] --> P13[13 Clipboard + dialogs]
    P12 --> P14[14 GameRunner runtime]
    P13 --> P15[15 USB + HID]
    P15 --> P16[16 Production networking]
    P16 --> P17[17 Wi-Fi + Network Manager]
    P15 --> P18[18 Bluetooth]
    P16 --> P20[20 Firewall]
    P19[19 NYFS maturity]
    P21[21 NotYVFirm]
```

Phases 13–21 are queued according to docs/roadmap.md. Windows PE/Win32 compatibility and Brave/VLC validation are explicitly excluded.
