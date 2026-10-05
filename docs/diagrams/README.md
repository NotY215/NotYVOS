# NOTYVOS Architecture Mapping

This directory holds editable architecture sources. Canonical facts remain
`docs/architecture.md`, `docs/roadmap.md` and `docs/data flow.md`. These files
are visualization layers, not a second specification.

GitHub renders **Mermaid** in Markdown. Everything else is source for its
own renderer. When a renderer is unavailable on GitHub, keep the source here
rather than claiming GitHub natively draws it.

## Catalog

| Source | Tool | Kind | How to view |
|---|---|---|---|
| Inlined in `docs/*.md` | Mermaid | Markdown-native flow / sequence | GitHub Markdown preview |
| `roadmap.markmap.md` | Markmap | Markdown-native mindmap | `npx markmap-cli roadmap.markmap.md` |
| `architecture.d2` | D2 | Infrastructure topology | `d2 architecture.d2 architecture.svg` |
| `truetype.d2` | D2 | Phase 11 TrueType pipeline | `d2 truetype.d2 truetype.svg` |
| `boot.d2` | D2 | Boot pipeline | `d2 boot.d2 boot.svg` |
| `architecture.ilograph.yaml` | Ilograph | Multi-perspective architecture | [app.ilograph.com](https://app.ilograph.com) |
| `architecture.eraser.md` | Eraser / DiagramGPT | Authoring aid | Paste the fenced DSL into Eraser |
| `architecture.excalidraw.md` | Excalidraw | Sketch-style source | Open the fenced JSON in Excalidraw |
| `architecture.excalidraw.json` | Excalidraw | Same canvas as a `.json` file | File → Open |
| `architecture-graph.html` | Cytoscape.js | Draggable dependency graph | Open the HTML file |
| `architecture.gojs.html` | GoJS | Layered interactive flowchart | Open the HTML file |
| `host-infrastructure.py` | Python Diagrams | Host/build/VM map | `python host-infrastructure.py` |

## Markdown-native usage in docs

Plain-text diagrams are also inlined in the docs they describe:

| Doc | Embedded sources |
|---|---|
| `docs/data flow.md` | Mermaid (all phases), D2 (RSX + TrueType), Markmap (Phase 11) |
| `docs/architecture.md` | Mermaid, D2, Markmap, Ilograph excerpt |
| `docs/boot-flow.md` | Mermaid sequence, D2 |
| `docs/fonts.md` | Mermaid, D2, Markmap |
| `docs/roadmap.md` | Mermaid, Markmap outline |
| `docs/build.md` | Mermaid, D2 |
| `docs/testing.md` | Mermaid |
| `docs/VM.md` | Mermaid, D2 |

## Tooling policy

- **Mermaid** is the GitHub-visible diagram. Node labels that contain `()` or
  `::` must be quoted (`DRAW["font::draw_text()"]`). Unquoted `font::draw_text()`
  is class / stadium syntax and fails to parse.
- **Markmap** consumes native Markdown bullets and headings.
- **D2** is the infrastructure layout language. Cross-container edges must use
  `Container.Child` paths.
- **Ilograph** is the multi-perspective YAML (`resources` + `perspectives`).
- **Eraser / DiagramGPT** may generate a first draft. Reconcile against the
  canonical docs before commit.
- **Excalidraw** stores the sketch source, not a binary `.excalidraw` export as
  the only copy.
- **Cytoscape.js** is the graph/network view.
- **GoJS** is the interactive layered flowchart. The HTML uses the evaluation
  GoJS build for documentation viewing only.
- **Python Diagrams** is reserved for host/build/VM infrastructure, not kernel
  internals.

## Phase 11 TrueType

The TrueType pipeline is mapped in:

- `docs/data flow.md` — Phase 11 Mermaid + sequence + D2 + Markmap
- `truetype.d2` — D2 source
- Ilograph perspective `TrueType`
- Desktop nodes in D2, Excalidraw, Cytoscape and GoJS
