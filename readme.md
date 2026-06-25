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
- Obsidian-friendly Markdown indexing for YAML frontmatter aliases/tags, inline tags, wikilinks, wikilink line anchors/relations, and block references.
- Markdown body content extraction for searchable notes without indexing frontmatter metadata as body text.
- Markdown task checkbox lines as searchable line anchors for direct jumps to actionable note items.
- Local relative Markdown links and Obsidian wikilinks as searchable aliases, line anchors, and automatic `links-to` relations when the target is indexed.
- Markdown external links as derived URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source note.
- Markdown reference-style external links as derived URL resources with source line anchors and `links-to` relations back to the source note.
- Lightweight PDF metadata extraction and page-level anchors.
- Lightweight PDF annotation region extraction for searchable `PdfRegion` anchors.
- Lightweight PDF text extraction from uncompressed, FlateDecode, ASCIIHexDecode, ASCII85Decode, RunLengthDecode, and LZWDecode text content streams, including literal octal escapes, UTF-16 BOM strings, ToUnicode CMaps, and ordered filter chains, for searchable PDF content.
- Local web shortcut indexing for `.url`, `.webloc`, and `.website` files as URL resources.
- URL fragment anchors for indexed web shortcuts.
- Standalone fallback opening preserves matched URL fragment anchors for web pages and local HTML pages.
- Local `.html` and `.htm` page indexing with title, canonical URL aliases, heading fragment anchors, and extracted searchable content.
- Browser bookmark export HTML and Chromium/Edge-style Bookmarks JSON indexing as individual URL resources with host aliases, folder aliases, bookmark tags, and URL fragment anchors.
- Chromium/Edge-style browser History SQLite indexing as individual URL resources with host aliases, visit-count aliases, URL fragment anchors, and `links-to` relations back to the history database.
- Firefox `places.sqlite` indexing as individual URL resources with bookmark titles, bookmark/folder aliases, visit-count aliases, URL fragment anchors, and `links-to` relations back to the places database.
- OPML subscription/link list indexing as individual URL resources with host aliases, feed aliases, folder aliases, feed tags, and URL fragment anchors.
- RSS and Atom feed XML indexing as individual URL resources with feed aliases, category tags, host aliases, and URL fragment anchors.
- Sitemap XML indexing as individual URL resources with sitemap tags, host aliases, and URL fragment anchors.
- HAR/http archive entry indexing as individual URL resources with page-title aliases, HTTP method/status tags, URL fragment anchors, source line anchors, and `links-to` relations back to the capture file.
- Plain-text URL list indexing from `.txt`, `.text`, `.log`, `.list`, `.links`, and `.urls` files as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source list.
- CSV/TSV URL-column indexing as individual URL resources with host aliases, table aliases, category/tag aliases, fragment anchors, source row anchors, and `links-to` relations back to the source table.
- JSON/JSONL URL string indexing as individual URL resources with host/path aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- YAML/TOML/INI/config URL string indexing as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Optional remote HTML fetching for indexed web shortcuts, with fetched page titles, canonical URL aliases, heading fragment anchors, and extracted searchable content.
- Lightweight content indexing for small plain-text, log, config, manifest, and tabular files, including TODO/FIXME/NOTE line anchors, config key/section/path anchors, package dependency anchors, and CSV/TSV column anchors.
- Code resource classification, lightweight symbol anchors, Rust/Go/Java/C# symbol coverage, C++ GoogleTest and JS/TS test case anchors, CMake project/target/package/test anchors, compile_commands.json line anchors and `compiles` relations, dependency/import line anchors including Go import blocks and JS/TS dynamic imports, and TODO/FIXME/NOTE line anchors for common engineering languages.
- Persistent manual and indexed related-resource links with a compact relationship summary in the locator panel.
- Persistent resource, anchor, and library-root recall signals for open count, last opened time, pinned resources, and pinned roots.
- Locator UI for creating manual aliases and anchors on selected resources.
- Locator UI records successful result activations and lets users pin/unpin selected resources.
- Built-in read-only text preview for line-based anchors.
- Cleaned-up search result display with readable type labels and tooltips.
- Search result tooltips summarize matched fields, matched anchors, and host-provided context matches.
- Search ordering that prioritizes anchors, titles, filenames, aliases, and tags before full-path matches.
- Search ordering gives exact title, filename, alias, tag, content, path, and anchor matches a small within-match-type boost.
- Search ordering applies small recall boosts for pinned, recent, and frequently opened resources without overriding match-type quality.
- Search ordering applies anchor-level recall boosts for frequently opened precise jumps.
- Search ordering applies root-level boosts for resources inside pinned library folders.
- Search filtering accepts host-required resource kinds, tags, and location prefixes, while ordering accepts host-provided context tags, location prefixes, related resource ids, and relation-label constraints for embedded project/document context.
- `pinloom_widgets` static library with repository-injected reusable `PinloomPanel`.
- Host-facing `PinloomPanelOptions`, `PinloomOpenTarget`, and `PinloomRelatedTarget` API so embedding hosts can choose embedded chrome, observe result-count/current-target changes, navigate results, inspect current related targets, trigger current-result activation, and intercept selected result activation with resource metadata, matched field, context matches, relation label/note, score, and anchor details.
- Public `PinloomPanel` search text, focus, current-target, related-target, indexing, remote-web-fetching, required kind/tag/location filtering, relation-label-scoped context-ranking, and atomic host-context snapshot methods for dock/global-control hosts.
- CTest registration that supplies Qt, Qt plugins, and MinGW runtime paths for Windows test runs.
- `pinloom_app` standalone Qt application entry point using a default SQLite database under `QStandardPaths::AppDataLocation`.
- Locator UI for adding/removing folders, refreshing selected/all folders, rebuilding the index, searching, and opening selected resources.
- `pinloom_core_smoke_test` validating basic alias/tag search and FTS5 schema exposure.
- `pinloom_sqlite_repository_test` validating SQLite initialization, persistence, search, and idempotent upsert behavior.
- `pinloom_directory_source_test` validating explicit-root directory indexing and idempotent repository upserts.
- `pinloom_widget_smoke_test` validating repository injection into the reusable widget.

Not implemented yet:

- Full PDF text/content extraction for remaining unsupported filters, complex encodings, and OCR; richer source-code parsing; broader web source support beyond local HTML, shortcuts, bookmarks/history/places with bookmark metadata, OPML, feeds, sitemaps, HAR/http archives, text URL lists, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.
- Full ZeroSlack dock/global-control integration.

Next:

- Full ZeroSlack dock/global-control integration.
- Fuller PDF text extraction for remaining unsupported filters/encodings and broader web source support beyond local HTML, shortcuts, bookmarks/history/places with bookmark metadata, OPML, feeds, sitemaps, HAR/http archives, text URL lists, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.

## Embedding Contract

Pinloom owns:

- `pinloom_core` models, repositories, source indexing, search, and SQLite schema evolution.
- `pinloom_widgets` locator UI and standalone fallback behavior for opening selected targets.
- Library root management and index refresh/rebuild workflows.

Embedding hosts own:

- Where the widget is placed, such as a dock, command palette, or global-control panel.
- Whether selected results open through host navigation, host previews, or Pinloom's fallback opener.
- Host-specific context such as the active project, active document, or preferred focus shortcut.

The core embedding API is `PinloomPanel(ILibraryRepository&, PinloomPanelOptions, QWidget*)`. A host can seed search through `setSearchText()`, focus the locator through `focusSearch()`, inspect result count through `resultCount()`, navigate results through `selectFirstResult()`, `selectNextResult()`, and `selectPreviousResult()`, inspect the current selection through `currentOpenTarget()`, inspect its related resources through `currentRelatedTargets()`, activate the current selection through `activateCurrentOpenTarget()`, observe result-count changes through `PinloomPanelOptions::resultCountChangedHandler`, observe selection changes through `PinloomPanelOptions::currentOpenTargetChangedHandler`, choose compact embedded chrome through `showLibraryRootControls`, `showManualEditControls`, and `showPinControls`, control optional remote HTML fetching through `setRemoteWebFetchingEnabled()`, trigger indexing through `indexSelectedLibraryRoot()`, `indexAllEnabledLibraryRoots()`, and `rebuildAllEnabledLibraryRoots()`, filter by required resource kinds, tags, and location prefixes through `setRequiredResourceKinds()`, `setRequiredTags()`, and `setRequiredLocationPrefixes()`, pass ranking context through `setContextTags()`, `setContextLocationPrefixes()`, `setContextResourceIds()`, and `setContextRelationLabels()`, or apply the active host search/filter/ranking state atomically through `applyHostContext(const PinloomHostContext&)`. Indexing calls return `PinloomIndexingResult` with success, indexed count, and error fields. The `PinloomOpenTarget` payload includes the selected resource id, kind, title, location, matched field, matched context tag/location prefix/resource relation label/note, score, and optional anchor. The `PinloomRelatedTarget` payload includes relation label/note, direction, and the related resource target.

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
- Tests: passed for host search control, embedded chrome options, result count/navigation/change notifications, current-target observation, current-related-target inspection, host-triggered current activation, indexing controls, required filters, tag/location/relation context ranking, relation-label-scoped context ranking, atomic host-context snapshots, host activation interception, and standalone fallback behavior (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Obsidian-Friendly Indexing MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for frontmatter aliases/tags, inline tags, wikilink aliases, wikilink line anchors/relations, Markdown body extraction, and block references (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

PDF Navigation MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Code-Aware Locator MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for code resource classification, C/C++/Python/JS/TS/HDL/Tcl/Rust/Go/Java/C# symbol anchors, C++ GoogleTest and JS/TS test case anchors, CMake project/target/package/test anchors, compile_commands.json line anchors and `compiles` relations, dependency/import line anchors including Go import blocks and JS/TS dynamic imports, TODO/FIXME/NOTE line anchors, and line-anchor UI display (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Manual Anchors And Relationships MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for related-resource persistence, indexed Markdown link relations, compact relationship summaries, host related-target inspection, and manual anchor/alias editing UI (`pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Ranking And Recall MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for resource usage persistence, anchor usage persistence, exact-match ranking, pinned-resource ranking, pinned-root ranking, host tag/location/relation context ranking, context tooltip details, usage preservation across resource updates, activation recording, anchor activation recording, and pin/unpin UI (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_widget_smoke_test`)

Integration And Source Refinement MVP validation:

- Configure: passed
- Build: passed
- Additional source-link validation: passed for plain-text URL list source line anchors and `links-to` relations (`pinloom_directory_source_test`)
- Tests: passed for Markdown body content extraction/search, Markdown task line anchors, local relative Markdown link aliases/anchors/relations, Markdown inline and reference-style external link URL resources with source line anchors and `links-to` relations, plain-text URL list resources, HAR/http archive entry resources, JSON/JSONL URL resources with source line anchors and `links-to` relations, YAML/TOML/INI/config URL resources with source line anchors and `links-to` relations, CSV/TSV URL-column resources with source row anchors and `links-to` relations, RSS/Atom feed entry resources, sitemap URL resources, plain-text file content indexing, action line anchors, config key/section/path anchors, package manifest dependency anchors, CSV/TSV column anchors, local web shortcut indexing, browser bookmark export/native JSON indexing, Chromium/Edge-style History SQLite indexing, Firefox places.sqlite indexing with bookmark metadata, OPML link/feed indexing, URL fragment anchors, URL fragment fallback opening, local HTML page content extraction/search, optional remote HTML fetch for web shortcuts, lightweight PDF text extraction/search for uncompressed, FlateDecode, ASCIIHexDecode, RunLengthDecode, LZWDecode, literal octal escapes, UTF-16 BOM strings, ToUnicode CMaps, and ordered filter chains, URL activation through host interception, PDF annotation region anchors, and PDF region open-target preservation (`pinloom_directory_source_test`, `pinloom_widget_smoke_test`)
