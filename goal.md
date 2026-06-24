# Pinloom Goal

## Long-Term Goal

Pinloom is a fast locator for personal and engineering knowledge. It helps users jump to the right material, anchor, note, file, PDF page, code line, folder, web page, alias, tag, or related resource without becoming a full-disk search tool.

## Architecture Principles

- Standalone first: Pinloom must run as a complete Qt application.
- Embeddable UI: reusable widgets must later fit into ZeroSlack as a dock/global-control panel.
- Core independence: `pinloom_core` must not depend on ZeroSlack UI or app-specific host behavior.
- Source neutrality: Obsidian vaults, normal folders, PDFs, code snippets, notes, and manual anchors are all library sources.
- Obsidian friendliness: markdown tags, aliases, headings, wikilinks, and block ids should be indexed without making Pinloom an Obsidian add-on.
- Precise anchors: search results should be able to land on PDF pages/regions, file lines, markdown headings/blocks, URLs, and manual targets.
- Locator search: optimize for curated library positioning and fast jumps, not whole-disk crawling.

## Milestones

### Project Bootstrap MVP

Completed:

- Initial Qt/CMake skeleton.
- `pinloom_core`, `pinloom_widgets`, and `pinloom_app` target split.
- Minimal data model and search/repository interfaces.
- In-memory repository for smoke testing and early UI wiring.
- SQLite/FTS5 schema draft.
- Reusable Qt widget and standalone app entry point.
- Core smoke test.

### Local Library Persistence MVP

Completed:

- SQLite repository implementation behind `ILibraryRepository`.
- Versioned schema initialization with `schema_migrations`.
- Persistent resources, tags, aliases, anchors, and FTS5 metadata rows.
- Repository lifecycle methods for open, initialize, and error reporting.
- SQLite persistence tests for initialization, metadata search, and multi-value readback.
