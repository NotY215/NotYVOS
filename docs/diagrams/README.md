# NOTYVOS Architecture Mapping

This directory contains editable architecture sources for different renderers.

| Source | Tool | Purpose |
|---|---|---|
| roadmap.markmap.md | Markmap | Interactive roadmap mindmap from native Markdown |
| architecture.d2 | D2 | Clean infrastructure architecture |
| architecture.ilograph.yaml | Ilograph | Multi-perspective system, desktop, storage and PS3 views |
| architecture.excalidraw.md | Excalidraw | Human-editable sketch-style architecture source |
| architecture-graph.html | Cytoscape.js | Interactive draggable/auto-layout dependency graph |

## Tooling policy

The canonical facts remain in docs/roadmap.md and docs/architecture.md. These files are visualization sources, not separate architecture specifications.

GoJS is reserved for future state-monitor or desktop interaction visualizations. Python Diagrams is reserved for host/build infrastructure maps. Eraser/DiagramGPT can be used as an authoring aid, but generated output must be reconciled against the canonical repository docs before commit.

When a renderer is unavailable in GitHub Markdown, keep its source here rather than claiming that GitHub natively renders it.