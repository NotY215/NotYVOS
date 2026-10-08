# NOTYVOS Roadmap

This is the canonical NOTYVOS roadmap. It supersedes the previous phase numbering.
Phases 0–10F are frozen as delivered. Phase 11, Phase 12, Phase 13A and Phase 15A–15E are delivered. The active milestone is init-program debugging. Windows .exe compatibility and Brave/VLC validation are explicitly excluded from the current queue.

## Roadmap map

Markmap source: [`diagrams/roadmap.markmap.md`](diagrams/roadmap.markmap.md).

```mermaid
flowchart TD
    D[Phases 0-10F delivered] --> P11[11 TrueType DONE]
    P11 --> P12[12 Explorer DONE]
    P12 --> P13A[13A Clipboard DONE]
    P13A --> I[Init debugging NEXT]
    P12 --> P14[14 / 7D GameRunner QUEUED]
    P13A --> P15[15A-15E USB + unified input DONE]
    P15 --> P16[16 Production network]
    P16 --> P17[17 Wi-Fi]
    P15 --> P18[18 Bluetooth]
    P16 --> P20[20 Firewall]
    P19[19 NYFS maturity]
    P21[21 NotYVFirm]
    X[Windows PE / Brave / VLC]:::ex
    classDef ex fill:#3b1f1f,stroke:#c44,color:#fff
```

- NOTYVOS roadmap
  - Delivered: 0–10F, 11 TrueType, 12 Explorer, 13A clipboard, 15A–15E USB + unified input
  - Next: Init program debugging
  - Queued: 13B–13C, 14/7D, 16–21
  - Excluded: Windows PE, Brave, VLC

## Current state

| Subsystem | Status | Approx. | Reality |
|---|---|---:|---|
| Kernel boot / GDT / IDT / TSS / SMP / syscalls | Delivered | ~92% | Missing production networking, firewall and USB |
| Memory (PMM, VMM, heap, exec arena) | Delivered | ~95% | Solid |
| Scheduler / tasks / fork / signals | Delivered | ~85% | Missing priorities, CPU affinity and real IPC |
| VFS / NYFS / initramfs | Delivered | ~55% | NYFS is development-grade; no journaling or recovery |
| Graphics (compositor, HAL, VBE, GPU API) | Delivered | ~85% | Solid for software rendering |
| RSX compatibility | Delivered | ~65% | FIFO, methods, raster, texture, UV, mip and depth; shaders/full method coverage remain |
| PPU / SPU / DMA / JIT | Delivered | ~70% | Working interpreter and baseline JIT; broader opcode coverage remains |
| PS3 ABI / cellFs / GameRunner | Delivered | ~55% | Load, run and file I/O; 7D runtime integration remains |
| Desktop UI | Delivered | ~70% | 10G Explorer, real file operations, clipboard, drag/drop and dialogs remain |
| Image decoders | Delivered | ~90% | BMP/PNG/GIF/ICO/JPEG are solid |
| SVG decoder + icons | Delivered | ~85% | Diagnostics added; VFS packaging confirmation remains |
| TrueType font renderer | Delivered | ~75% | Inter rasterization and AA text delivered; shaping and kerning remain |
| Hardware support | Partial | ~35% | ACPI, AHCI, e1000, HDA, PS/2, USB; RTL8188EU Wi-Fi firmware input is now defined, but full Wi-Fi integration remains |
| Native firmware (NotYVFirm) | Not started | 0% | Long-term firmware domain |
| Windows .exe compatibility | Excluded | 0% | Not on the current roadmap |
| Brave / VLC validation | Excluded | 0% | Not on the current roadmap |

## Frozen delivered boundary: Phases 0–10

All previously defined Phase 0–10 work is frozen as delivered, including desktop
polish through 10F, RSX work through 6F, rendering validation, image support,
themes, Appearance settings, snap layouts, taskbar groups, SVG icons and
Explorer navigation history.

Intentional Phase 2 gaps **2H** and **2J** remain absent.

## Phase 11 -- TrueType Font Subsystem -- DONE

Goal: scalable anti-aliased TrueType rendering using the Inter family.

| Subphase | Status | Scope |
|---|---|---|
| 11A | Done | TTF parser + outline extraction |
| 11B | Done | Coverage rasterizer |
| 11C | Done | Per-face glyph cache |
| 11D | Done | Compositor integration |
| 11E | Done | Boot self-test |

Delivered: head, hhea, hmtx, maxp, cmap 4/12, loca and glyf parsing; simple
glyph outlines; quadratic Bézier flattening; 4× vertical supersampling; 512-entry
age-based per-face cache; `font::draw_text()` integration; default Inter Regular
loading.

Deferred polish: multiple weights, kerning, complex-script shaping and subpixel
horizontal rendering.

## Phase 12 -- Explorer 10G + Real File Operations -- DONE

**Dependency:** Phase 11.

| Subphase | Status | Scope | Exit criteria |
|---|---|---|---|
| 12A | **Done** | Grid view + details toggle | Toolbar switches modes and remembers the session |
| 12B | **Done** | XP-style breadcrumb | Clickable path segments navigate to ancestors |
| 12C | **Done** | Real file operations | Open/New/Rename/Delete/Properties persist through VFS/NYFS |

12C will use/expose: `fs::vfs_create_file`, `fs::vfs_mkdir`,
`fs::vfs_rename`, `fs::vfs_unlink`, and `fs::vfs_write`.

Deferred from Phase 12: drag/drop, multi-select and Explorer search.

## Phase 13 -- Desktop Clipboard + Dialogs -- PARTIAL

**Dependency:** Phase 12.

| Subphase | Scope |
|---|---|
| 13A | **Done** | Global clipboard, text/file lists, Ctrl+C/X/V routing |
| 13B | Blocking input and confirmation dialogs |
| 13C | Read-only Properties dialog with real metadata |

Deferred: rich-text clipboard, image clipboard and multi-item paste ordering.

## Phase 14 -- 7D GameRunner Runtime Integration -- QUEUED

**Dependency:** Phase 12.

| Subphase | Scope |
|---|---|
| 14A | Session lifecycle: start/step/pause/resume/stop and dedicated task |
| 14B | Persistent per-game configuration |
| 14C | cellSaveData bridge to NYFS |

Universal save states remain deferred.

## Phase 15 -- USB Stack + USB HID -- DONE

| Subphase | Scope |
|---|---|
| 15A | **Done** | PCI enumeration + xHCI controller |
| 15B | **Done** | USB device enumeration |
| 15C | **Done** | USB HID keyboard/mouse |
| 15D | **Done** | USB mass storage |
| 15E | **Done** | Unified input facade and routing |

Deferred: hubs, USB 3.x SuperSpeed and isochronous transfers.

## Phase 16 -- Production Network Stack -- QUEUED

**Dependency:** Phase 15 optional.

| Subphase | Scope |
|---|---|
| 16A | Ethernet, ARP and IPv4 |
| 16B | ICMP and UDP |
| 16C | TCP |
| 16D | DHCP and DNS |
| 16E | User-space socket API |

Deferred: IPv6, IPsec, multicast and raw sockets.

## Phase 17 -- Wi-Fi Driver + Management UI -- QUEUED

**Dependency:** Phase 16.

| Subphase | Scope |
|---|---|
| 17A | RTL8188EU 802.11 driver and `Firmware/rtl8188eufw.bin` firmware loading |
| 17B | WPA2 supplicant |
| 17C | Network Manager UI in Settings |
| 17D | Roaming and power management |

Deferred: WPA3, 802.1X enterprise and monitor mode.

## Phase 18 -- Bluetooth Framework -- QUEUED

**Dependency:** Phase 15 optional.

| Subphase | Scope |
|---|---|
| 18A | HCI + USB transport |
| 18B | L2CAP + RFCOMM |
| 18C | Bluetooth HID |
| 18D | Pairing UI |

Deferred: BLE and audio profiles.

## Phase 19 -- NYFS Maturity -- QUEUED

| Subphase | Scope |
|---|---|
| 19A | Write-ahead journaling |
| 19B | Crash recovery |
| 19C | Dynamic directory/inode scaling |
| 19D | CRC32 block integrity and scrub |

Deferred: snapshots, deduplication, compression and ACLs.

## Phase 20 -- Firewall + Network Security -- QUEUED

**Dependency:** Phase 16.

| Subphase | Scope |
|---|---|
| 20A | Stateful IP/TCP packet filter |
| 20B | Per-application rules |
| 20C | Firewall UI in Settings |

Deferred: intrusion detection, deep packet inspection and VPN support.

## Phase 21 -- NotYVFirm -- QUEUED

Long-term native firmware domain replacing the Limine/UEFI boot dependency.

| Subphase | Scope |
|---|---|
| 21A | Firmware architecture specification |
| 21B | NotYVFirm UEFI boot path |
| 21C | Optional BIOS/legacy path |
| 21D | Firmware configuration UI |
| 21E | Signed updates and rollback |

Deferred indefinitely: Secure Boot integration and TPM measurements.

## Honest gap list

### Core OS
1. Production IPv4/IPv6, TCP, UDP, DHCP, DNS and sockets
2. USB host controller, USB HID and USB storage
3. Wi-Fi driver and management, including RTL8188EU firmware loading
4. Bluetooth framework
5. Firewall and network security
6. NYFS journaling, crash recovery and scaling
7. NotYVFirm

### Desktop / UI
8. Explorer 10G grid/details/breadcrumb
9. Real Explorer file operations
10. File-operation dialogs
11. Clipboard subsystem
12. Drag-and-drop file management
13. Multiple Inter font weight selection
14. Complex-script shaping and kerning
15. Application lifecycle management

### PS3 / compatibility
16. 7D GameRunner runtime/session integration
17. Full PS3 system-call coverage
18. Complete PPU opcode coverage
19. Complete SPU ecosystem coverage
20. Complete RSX method and shader-model coverage

## Explicit exclusions

- Windows PE loading, Win32/Win64 API surface, registry, COM and SEH
- Brave browser validation
- VLC media-player validation

These are not current roadmap phases.

## Delivery contract

Each phase delivery documents architecture, component purpose, dependencies,
file structure, data flow, implementation, build changes, tests, expected result,
limitations, next milestone and roadmap status.

Every subsystem gets a boot self-test. Warnings are errors, casts use
`static_cast`, magic numbers are avoided and dead code is not retained.

## Roadmap status

| Phase | Name | Status |
|---|---|---|
| 0–10F | Foundation through desktop polish | **Done** |
| 11 | TrueType Font Subsystem | **Done** |
| 12 | Explorer 10G + Real File Operations | **Done** |
| 13A | Desktop Clipboard | **Done** |
| 13B–13C | Remaining clipboard/dialog work | Queued |
| Fix | User-fault isolation + user build flags + BMP test vector | **Delivered** |
| Init | Init program debugging | **Next** |
| 14 / 7D | GameRunner Runtime Integration | Queued |
| 15A–15E | USB Stack + Unified Input | **Done** |
| 16 | Production Network Stack | Queued |
| 17 | Wi-Fi Driver + Management UI | Queued |
| 18 | Bluetooth Framework | Queued |
| 19 | NYFS Maturity | Queued |
| 20 | Firewall + Network Security | Queued |
| 21 | NotYVFirm | Queued |
| -- | Windows .exe compatibility | **Excluded** |
| -- | Brave / VLC validation | **Excluded** |

**Next delivery: Init program debugging in `user/init/main.c`.**


## Current continuation milestone

The next active engineering target is `user/init/main.c`. Resolve the current init-program build/runtime issue before advancing to Phase 14 / 7D unless explicitly requested otherwise.

The delivered ISR exception-dispatch fix is commit `ef79fc3f31e0c6f1574cb2e29028ce4e91dcdb1b`. Kernel-mode exceptions remain fatal; user-mode exceptions are isolated to the offending task through the scheduler exit path.
