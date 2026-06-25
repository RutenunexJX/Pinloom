# Pinloom

Pinloom is a standalone Qt application for locating personal and engineering materials: files, folders, PDFs, code snippets, web pages, notes, tags, aliases, anchors, and jump relationships.

It is not an Obsidian add-on and is not a ZeroSlack-private feature. Obsidian vaults are one supported library source. ZeroSlack embedding is a later UI host target.

## Current MVP: Integration And Source Refinement

Implemented:

- Qt/CMake project skeleton.
- `pinloom_core` static library with resource, anchor, search, repository interfaces, an in-memory repository, and a SQLite/FTS5 schema draft.
- SQLite-backed `SqliteLibraryRepository` with versioned schema initialization.
- Persistent resources, tags, aliases, anchors, and FTS5 metadata search.
- `LibrarySource` and `IndexingService` abstractions.
- `DirectoryLibrarySource` for explicit normal-directory indexing.
- Persistent library root management in SQLite with schema version 2.
- Saved root CRUD in `ILibraryRepository`, `InMemoryLibraryRepository`, and `SqliteLibraryRepository`.
- `IndexingService` refresh/rebuild support for saved enabled roots.
- Anchor-aware search results with optional matched anchors.
- SQLite schema version 3 with anchor FTS search.
- Markdown heading and block-id anchor extraction for `.md` and `.markdown` files.
- Obsidian-friendly Markdown indexing for YAML frontmatter aliases/tags, inline tags, wikilinks, and block references.
- Markdown body content extraction for searchable notes without indexing frontmatter metadata as body text.
- Markdown task checkbox lines as searchable line anchors for direct jumps to actionable note items.
- Local relative Markdown links as searchable aliases, line anchors, and automatic `links-to` relations when the target is indexed.
- Markdown external links as derived URL resources with host aliases and fragment anchors.
- Lightweight PDF metadata extraction and page-level anchors.
- Lightweight PDF annotation region extraction for searchable `PdfRegion` anchors.
- Lightweight PDF text extraction from uncompressed, FlateDecode, ASCIIHexDecode, ASCII85Decode, and RunLengthDecode text content streams, including literal octal escapes and ordered filter chains, for searchable PDF content.
- Local web shortcut indexing for `.url`, `.webloc`, and `.website` files as URL resources.
- URL fragment anchors for indexed web shortcuts.
- Standalone fallback opening preserves matched URL fragment anchors for web pages and local HTML pages.
- Local `.html` and `.htm` page indexing with title, canonical URL aliases, heading fragment anchors, and extracted searchable content.
- Browser bookmark export HTML and Chromium/Edge-style Bookmarks JSON indexing as individual URL resources with host aliases, folder aliases, bookmark tags, and URL fragment anchors.
- OPML subscription/link list indexing as individual URL resources with host aliases, feed aliases, folder aliases, feed tags, and URL fragment anchors.
- Optional remote HTML fetching for indexed web shortcuts, with fetched page titles, canonical URL aliases, heading fragment anchors, and extracted searchable content.
- Lightweight content indexing for small plain-text, log, config, manifest, and tabular files, including TODO/FIXME/NOTE line anchors, config key/section/path anchors, package dependency anchors, and CSV/TSV column anchors.
- Code resource classification, lightweight symbol anchors, Rust/Go/Java/C# symbol coverage, C++ GoogleTest and JS/TS test case anchors, dependency/import line anchors including Go import blocks and JS/TS dynamic imports, and TODO/FIXME/NOTE line anchors for common engineering languages.
- Persistent manual and indexed related-resource links with a compact relationship summary in the locator panel.
- Persistent resource, anchor, and library-root recall signals for open count, last opened time, pinned resources, and pinned roots.
- Locator UI for creating manual aliases and anchors on selected resources.
- Locator UI records successful result activations and lets users pin/unpin selected resources.
- Built-in read-only text preview for line-based anchors.
- Cleaned-up search result display with readable type labels and tooltips.
- Search result tooltips summarize matched fields, matched anchors, and host-provided context matches.
- Search ordering that prioritizes anchors, titles, filenames, aliases, and tags before full-path matches.
- Search ordering applies small recall boosts for pinned, recent, and frequently opened resources without overriding match-type quality.
- Search ordering applies anchor-level recall boosts for frequently opened precise jumps.
- Search ordering applies root-level boosts for resources inside pinned library folders.
- Search filtering accepts host-required resource kinds, tags, and location prefixes, while ordering accepts host-provided context tags and location prefixes for embedded project/document context.
- `pinloom_widgets` static library with repository-injected reusable `PinloomPanel`.
- Host-facing `PinloomPanelOptions` and `PinloomOpenTarget` API so embedding hosts can observe current-target changes and intercept selected result activation with resource metadata, matched field, context matches, score, and anchor details.
- Public `PinloomPanel` search text, focus, current-target, indexing, remote-web-fetching, required kind/tag/location filtering, and context-ranking methods for dock/global-control hosts.
- CTest registration that supplies Qt, Qt plugins, and MinGW runtime paths for Windows test runs.
- `pinloom_app` standalone Qt application entry point using a default SQLite database under `QStandardPaths::AppDataLocation`.
- Locator UI for adding/removing folders, refreshing selected/all folders, rebuilding the index, searching, and opening selected resources.
- `pinloom_core_smoke_test` validating basic alias/tag search and FTS5 schema exposure.
- `pinloom_sqlite_repository_test` validating SQLite initialization, persistence, search, and idempotent upsert behavior.
- `pinloom_directory_source_test` validating explicit-root directory indexing and idempotent repository upserts.
- `pinloom_widget_smoke_test` validating repository injection into the reusable widget.

Not implemented yet:

- Full PDF text/content extraction for unsupported filters, complex encodings, and OCR; richer source-code parsing; broader web source support beyond shortcuts, browser bookmark files, and OPML.
- Full ZeroSlack dock/global-control integration.

Next:

- Full ZeroSlack dock/global-control integration.
- Fuller PDF text extraction and broader web source support beyond shortcuts, browser bookmark files, and OPML.

## Embedding Contract

Pinloom owns:

- `pinloom_core` models, repositories, source indexing, search, and SQLite schema evolution.
- `pinloom_widgets` locator UI and standalone fallback behavior for opening selected targets.
- Library root management and index refresh/rebuild workflows.

Embedding hosts own:

- Where the widget is placed, such as a dock, command palette, or global-control panel.
- Whether selected results open through host navigation, host previews, or Pinloom's fallback opener.
- Host-specific context such as the active project, active document, or preferred focus shortcut.

The core embedding API is `PinloomPanel(ILibraryRepository&, PinloomPanelOptions, QWidget*)`. A host can seed search through `setSearchText()`, focus the locator through `focusSearch()`, inspect the current selection through `currentOpenTarget()`, observe selection changes through `PinloomPanelOptions::currentOpenTargetChangedHandler`, control optional remote HTML fetching through `setRemoteWebFetchingEnabled()`, trigger indexing through `indexSelectedLibraryRoot()`, `indexAllEnabledLibraryRoots()`, and `rebuildAllEnabledLibraryRoots()`, filter by required resource kinds, tags, and location prefixes through `setRequiredResourceKinds()`, `setRequiredTags()`, and `setRequiredLocationPrefixes()`, pass ranking context through `setContextTags()` and `setContextLocationPrefixes()`, and intercept result activation through `PinloomPanelOptions::openTargetHandler`. Indexing calls return `PinloomIndexingResult` with success, indexed count, and error fields. The `PinloomOpenTarget` payload includes the selected resource id, kind, title, location, matched field, matched context tag/location prefix, score, and optional anchor.

## Build

The first verified local toolchain is Qt 6.10.2 with MinGW and CMake/Ninja from `E:\QT6`.

```powershell
$env:PATH='E:\QT6\Tools\mingw1310_64\bin;' + $env:PATH
E:\QT6\Tools\CMake_64\bin\cmake.exe -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=E:\QT6\6.10.2\mingw_64 -DCMAKE_MAKE_PROGRAM=E:\QT6\Tools\Ninja\ninja.exe -DCMAKE_CXX_COMPILER=E:\QT6\Tools\mingw1310_64\bin\g++.exe
E:\QT6\Tools\CMake_64\bin\cmake.exe --build build
E:\QT6\Tools\CMake_64\bin\ctest.exe --test-dir build --output-on-failure
```

## Run

```powershell
.\build\pinloom_app.exe
```

## Validation

Project Bootstrap MVP validation:

- Configure: passed with Qt 6.10.2 / MinGW / Ninja
- Build: passed
- Smoke test: passed (`pinloom_core_smoke_test`)

Local Library Persistence MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`)

Directory Source Indexing MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`)

Usable Locator UI MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Library Root Management MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Precise Text and Markdown Jumps MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Search Results Cleanup MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

ZeroSlack Embedding Boundary MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Obsidian-Friendly Indexing MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

PDF Navigation MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Code-Aware Locator MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for code resource classification, C/C++/Python/JS/TS/HDL/Tcl/Rust/Go/Java/C# symbol anchors, C++ GoogleTest and JS/TS test case anchors, dependency/import line anchors including Go import blocks and JS/TS dynamic imports, TODO/FIXME/NOTE line anchors, and line-anchor UI display (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Manual Anchors And Relationships MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for related-resource persistence, indexed Markdown link relations, compact relationship summaries, and manual anchor/alias editing UI (`pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Ranking And Recall MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for resource usage persistence, anchor usage persistence, pinned-resource ranking, pinned-root ranking, host context ranking, context tooltip details, usage preservation across resource updates, activation recording, anchor activation recording, and pin/unpin UI (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_widget_smoke_test`)

Integration And Source Refinement MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for Markdown body content extraction/search, Markdown task line anchors, local relative Markdown link aliases/anchors/relations, Markdown external link URL resources, plain-text file content indexing, action line anchors, config key/section/path anchors, package manifest dependency anchors, CSV/TSV column anchors, local web shortcut indexing, browser bookmark export/native JSON indexing, OPML link/feed indexing, URL fragment anchors, URL fragment fallback opening, local HTML page content extraction/search, optional remote HTML fetch for web shortcuts, lightweight PDF text extraction/search for uncompressed, FlateDecode, ASCIIHexDecode, ASCII85Decode, RunLengthDecode, literal octal escapes, and ordered filter chains, URL activation through host interception, PDF annotation region anchors, and PDF region open-target preservation (`pinloom_directory_source_test`, `pinloom_widget_smoke_test`)
