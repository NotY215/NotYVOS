# NOTYVOS Roadmap Mindmap

Markmap parses this native Markdown outline into an interactive mindmap
(`npx markmap-cli docs/diagrams/roadmap.markmap.md`).

- NOTYVOS
  - Delivered
    - Phase 0–10F
      - Kernel / CPU / memory
      - Processes / VFS / NYFS
      - Desktop compositor
      - RSX 6A–6F
      - GameRunner 7A–7C
      - Rendering validation 8A–8B
      - Image codecs 9A–9C
      - Themes / Settings 10A–10F
    - Phase 11 -- TrueType Font Subsystem
      - 11A Parser
        - head, hhea, hmtx, maxp
        - cmap 4 / 12
        - loca + glyf
      - 11B Rasterizer
        - Simple outlines
        - Quadratic Bezier flatten
        - 4x coverage AA
      - 11C Glyph cache
        - 512-entry per-face
        - Age-based eviction
      - 11D Compositor integration
        - font::draw_text()
      - 11E Boot self-test
  - Next
    - Phase 12 -- Explorer 10G + Real File Operations
      - 12A Grid + details
      - 12B Breadcrumb
      - 12C Open / New / Rename / Delete / Properties
  - Queued
    - Phase 13 -- Clipboard + Dialogs
    - Phase 14 -- GameRunner Runtime Integration
    - Phase 15 -- USB + HID
    - Phase 16 -- Production Network -- QUEUED
    - Phase 17 -- Wi-Fi + Network Manager -- QUEUED
    - Phase 18 -- Bluetooth -- QUEUED
    - Phase 19 -- NYFS Maturity -- QUEUED
    - Phase 20 -- Firewall -- QUEUED
    - Phase 21 -- NotYVFirm -- QUEUED
  - Explicitly excluded
    - Windows PE / Win32 / Win64 compatibility
    - Brave validation
    - VLC validation
