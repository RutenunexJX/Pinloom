# Pinloom

Pinloom is a standalone Qt application for locating personal and engineering materials: files, folders, PDFs, text beacons, web pages, notes, tags, aliases, anchors, and jump relationships.

It is not an Obsidian add-on and is not a ZeroSlack-private feature. Obsidian vaults are one supported library source. ZeroSlack embedding is a later UI host target.

## Product Boundary

Pinloom is a beacon/location indexer and jump layer. It is not a code intelligence engine, a multilingual IDE, or a language semantic analyzer.

- Text-like files are treated uniformly. Verilog/SV, Tcl, XDC, C/C++, Python, JS, YAML, JSON, Markdown, logs, and plain text all feed the same text beacon model.
- Pinloom extracts searchable positions: line text, headings/sections, TODO/FIXME/NOTE, URLs, errors/warnings, user markers, and neutral symbol-like text. It does not build ASTs or claim programming-language support.
- Special file readers are reserved for formats that need them, such as PDF, Office documents, Visio, SQLite databases, binary archives, web/archive captures, and scanned/OCR material. Their purpose is still beacon and location extraction.
- When embedded in ZeroSlack, Pinloom provides paths, line/column locations when available, display text, beacons, and jump targets. ZeroSlack owns Verilog/SystemVerilog editing, HDL semantics, rendering, and the jump action.

## Special File Reader Phases

Dedicated readers are staged so each phase improves location extraction without expanding Pinloom into a knowledge-modeling system.

1. Existing reader consolidation: describe and test current PDF, browser SQLite, HTML/MHTML, HAR/WARC, OPML/RSS, sitemap/robots, iCalendar, email, structured text, and tabular readers as beacon/location readers.
2. Office baseline readers: extract Word/docx paragraph, heading, table, comment, and link beacons; Excel/xlsx sheet, cell, header, formula, error-value, named-range, and link beacons; PowerPoint slide, title, body, notes, and link beacons.
3. Engineering/design readers: extract Visio/vsdx page, shape text, connector label, and link beacons; generic SQLite table/column/URL/sample-value beacons; archive file-list, manifest, and text-preview beacons.
4. Scanned/OCR readers: extract OCR text from scanned PDFs and image-heavy documents with page/region anchors, confidence diagnostics, and page-level fallback jumps.
5. Reader contract and quality layer: standardize limits, output fields, position types, partial extraction, unsupported/encrypted/too-large states, timeouts, cancellation, and diagnostics.
6. Experience and performance: add reader toggles, incremental indexing, failure UI, jump fallbacks, deduplication, ranking/noise tuning, and large-file budgets.

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
- PDF outline/bookmark titles indexed as searchable page anchors when direct, named, or indirect named destinations point at pages.
- Lightweight PDF annotation region extraction for searchable `PdfRegion` anchors.
- PDF URI link annotations indexed as URL resources with fragment anchors and `links-to` relations back to the PDF.
- Lightweight PDF text extraction from uncompressed, FlateDecode, ASCIIHexDecode, ASCII85Decode, RunLengthDecode, and LZWDecode text content streams, including `TJ` text arrays with split glyph runs, literal octal escapes, UTF-16 BOM strings, ToUnicode CMaps, and ordered filter chains, for searchable PDF content.
- Local web shortcut indexing for `.url`, `.webloc`, `.website`, and `.desktop` link files as URL resources with shortcut URL/title line anchors.
- URL fragment anchors for indexed web shortcuts.
- Standalone fallback opening preserves matched URL fragment anchors for web pages and local HTML pages.
- Local `.html` and `.htm` page indexing with title, canonical URL aliases, heading fragment anchors, extracted searchable content, outbound URL resources, source line anchors, and `links-to` relations back to the source page.
- Local `.mhtml` and `.mht` web archive indexing with title, canonical URL aliases, heading fragment anchors, extracted searchable content, outbound URL resources, and `links-to` relations back to the archive.
- Browser bookmark export HTML indexing as individual URL resources with host aliases, bookmark tags, URL fragment anchors, source line anchors, and `links-to` relations back to the export file.
- Chromium/Edge-style Bookmarks JSON indexing as individual URL resources with host aliases, folder aliases, bookmark tags, URL fragment anchors, source line anchors, and `links-to` relations back to the Bookmarks file.
- XBEL bookmark XML indexing as individual URL resources with host/folder aliases, bookmark tags, URL fragment anchors, source line anchors, and `links-to` relations back to the XBEL file.
- Chromium/Edge-style browser History SQLite indexing as individual URL resources with host aliases, visit-count aliases, URL fragment anchors, and `links-to` relations back to the history database.
- Firefox `places.sqlite` indexing as individual URL resources with bookmark titles, bookmark/folder aliases, visit-count aliases, URL fragment anchors, and `links-to` relations back to the places database.
- Generic SQLite database indexing with table, column, URL, and sample-value beacons on the source database resource.
- ZIP archive indexing with file-list and manifest beacons on the source archive resource.
- OPML subscription/link list indexing as individual URL resources with host aliases, feed aliases, folder aliases, feed tags, URL fragment anchors, source line anchors, and `links-to` relations back to the OPML file.
- RSS and Atom feed XML indexing as individual URL resources with feed aliases, category tags, host aliases, URL fragment anchors, source line anchors, and `links-to` relations back to the feed file.
- Sitemap XML indexing as individual URL resources with sitemap tags, host aliases, URL fragment anchors, source line anchors, and `links-to` relations back to the source file.
- `robots.txt` Sitemap directive indexing as individual URL resources with robots/sitemap tags, source line anchors, and `links-to` relations back to the source file.
- HAR/http archive entry indexing as individual URL resources with page-title aliases, HTTP method/status tags, URL fragment anchors, source line anchors, and `links-to` relations back to the capture file.
- WARC response record indexing as individual URL resources with extracted HTML content, host aliases, URL fragment anchors, source line anchors, and `links-to` relations back to the WARC file.
- iCalendar `.ics`/`.ical` event indexing with event/time/location line anchors plus event URL resources, URL fragment anchors, and `links-to` relations back to the source calendar.
- Email `.eml` message indexing with subject/from/to/date line anchors plus body URL resources, URL fragment anchors, searchable message content, and `links-to` relations back to the source message.
- Plain-text URL list indexing from `.txt`, `.text`, `.log`, `.list`, `.links`, and `.urls` files as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source list.
- CSV/TSV URL-column indexing as individual URL resources with host aliases, table aliases, category/tag aliases, fragment anchors, source row anchors, and `links-to` relations back to the source table.
- JSON/JSONL URL string indexing as individual URL resources with host/path aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- YAML/TOML/INI/config URL string indexing as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Optional remote HTML fetching for indexed web shortcuts, with fetched page titles, canonical URL aliases, heading fragment anchors, and extracted searchable content.
- Lightweight content indexing for small plain-text, log, config, manifest, and tabular files, including TODO/FIXME/NOTE line anchors, config key/section/path anchors, package dependency anchors, and CSV/TSV column anchors.
- Unified text beacon indexing for source-like, log, config, manifest, and plain-text files, including TODO/FIXME/NOTE, URL, error/warning, section-like, user marker, and simple symbol-like line anchors.
- Build/config beacons for CMake project/target/package/test lines, Makefile targets, Dockerfile stage/base/copy lines, GitHub Actions workflow/job/step/action/run lines, GitLab CI stage/job/needs/script lines, and compile_commands.json line anchors with `compiles` relations.
- Persistent manual and indexed related-resource links with host-facing relation editing and a compact relationship summary in the locator panel.
- Persistent resource, anchor, and library-root recall signals for open count, last opened time, pinned resources, and pinned roots.
- Locator UI and host-facing API for creating manual aliases and anchors on selected resources or explicit resource ids.
- Locator UI and host-facing API record successful result activations and let callers pin/unpin selected resources or explicit resource ids.
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
- Host-facing `PinloomPanelOptions`, `PinloomOpenTarget`, `PinloomRelatedTarget`, and `PinloomLibraryRootTarget` API so embedding hosts can choose embedded chrome, observe result-count/current-target/result-snapshot/library-root/status/indexing-result changes, inspect current result, explicit resource, and library-root snapshots, add/remove, navigate, directly select, enable/disable, and pin/unpin library roots, inspect related targets by current selection or resource id, trigger current-result or resource-id activation, and intercept selected result activation with resource metadata, result row, matched field, context matches, relation label/note, score, match summary, and anchor details.
- Public `PinloomPanel` search text, focus, current-target, resource-id target, result-snapshot, library-root, related-target, status, indexing, latest-indexing-result, remote-web-fetching, required kind/tag/location filtering, relation-label-scoped context-ranking, and atomic host-context snapshot methods for dock/global-control hosts.
- CTest registration that supplies Qt, Qt plugins, and MinGW runtime paths for Windows test runs.
- `pinloom_app` standalone Qt application entry point using a default SQLite database under `QStandardPaths::AppDataLocation`.
- Locator UI for adding/removing folders, refreshing selected/all folders, rebuilding the index, searching, and opening selected resources.
- `pinloom_core_smoke_test` validating basic alias/tag search and FTS5 schema exposure.
- `pinloom_sqlite_repository_test` validating SQLite initialization, persistence, search, and idempotent upsert behavior.
- `pinloom_directory_source_test` validating explicit-root directory indexing and idempotent repository upserts.
- `pinloom_widget_smoke_test` validating repository injection into the reusable widget.

Not implemented yet:

- Full PDF text/content extraction for remaining unsupported filters, complex encodings, and OCR; Office baseline readers; Visio/vsdx, generic SQLite, and archive readers; broader web source support beyond local HTML/MHTML/WARC, desktop shortcuts, bookmarks/XBEL/history/places with bookmark metadata, OPML, feeds, sitemaps/robots.txt hints, HAR/http archives, iCalendar/email files, text URL lists, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.
- Full ZeroSlack dock/global-control integration.

Next:

- Full ZeroSlack dock/global-control integration.
- Special-file reader phase 1 consolidation, then Office baseline readers.
- Fuller PDF text extraction for remaining unsupported filters/encodings, OCR, and broader web source support beyond local HTML/MHTML/WARC, desktop shortcuts, bookmarks/XBEL/history/places with bookmark metadata, OPML, feeds, sitemaps/robots.txt hints, HAR/http archives, iCalendar/email files, text URL lists, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.

## Embedding Contract

Pinloom owns:

- `pinloom_core` models, repositories, source indexing, search, and SQLite schema evolution.
- `pinloom_widgets` locator UI and standalone fallback behavior for opening selected targets.
- Library root management and index refresh/rebuild workflows.

Embedding hosts own:

- Where the widget is placed, such as a dock, command palette, or global-control panel.
- Whether selected results open through host navigation, host previews, or Pinloom's fallback opener.
- Host-specific context such as the active project, active document, or preferred focus shortcut.

The core embedding API is `PinloomPanel(ILibraryRepository&, PinloomPanelOptions, QWidget*)`. A host can seed search through `setSearchText()`, focus the locator through `focusSearch()`, inspect result count through `resultCount()`, inspect result payloads through `resultAt()`, `currentResults()`, and `openTargetForResourceId()`, inspect library roots through `libraryRoots()` and `selectedLibraryRoot()`, navigate results through `selectFirstResult()`, `selectNextResult()`, and `selectPreviousResult()`, directly select results through `selectResultAt()` and `selectResultResource()`, add or remove library roots through `addLibraryRootPath()`, `removeSelectedLibraryRoot()`, and `removeLibraryRootById()`, directly select library roots through `selectLibraryRootById()`, enable or disable library roots through `setSelectedLibraryRootEnabled()` and `setLibraryRootEnabledById()`, pin or unpin library roots through `setSelectedLibraryRootPinned()` and `setLibraryRootPinnedById()`, add manual aliases and anchors through `addAliasToSelectedResource()`, `addAliasToResource()`, `addManualAnchorToSelectedResource()`, and `addManualAnchorToResource()`, pin or unpin resources through `setSelectedResourcePinned()` and `setResourcePinnedById()`, create, update, or remove relations through `upsertResourceRelation()` and `removeResourceRelation()`, inspect the current selection through `currentOpenTarget()`, inspect related resources through `currentRelatedTargets()` and `relatedTargetsForResource()`, inspect status text through `statusText()`, activate the current selection through `activateCurrentOpenTarget()`, activate explicit resources through `activateResourceById()`, observe result-count changes through `PinloomPanelOptions::resultCountChangedHandler`, observe result snapshot changes through `PinloomPanelOptions::resultsChangedHandler`, observe library-root snapshot and selection changes through `PinloomPanelOptions::libraryRootsChangedHandler` and `PinloomPanelOptions::currentLibraryRootChangedHandler`, observe status changes through `PinloomPanelOptions::statusChangedHandler`, observe selection changes through `PinloomPanelOptions::currentOpenTargetChangedHandler`, choose compact embedded chrome through `showLibraryRootControls`, `showManualEditControls`, and `showPinControls`, control optional remote HTML fetching through `setRemoteWebFetchingEnabled()`, trigger indexing through `indexSelectedLibraryRoot()`, `indexLibraryRootById()`, `indexAllEnabledLibraryRoots()`, and `rebuildAllEnabledLibraryRoots()`, filter by required resource kinds, tags, and location prefixes through `setRequiredResourceKinds()`, `setRequiredTags()`, and `setRequiredLocationPrefixes()`, pass ranking context through `setContextTags()`, `setContextLocationPrefixes()`, `setContextResourceIds()`, and `setContextRelationLabels()`, or apply the active host search/filter/ranking state atomically through `applyHostContext(const PinloomHostContext&)`. Indexing calls return `PinloomIndexingResult` with success, indexed count, and error fields. The `PinloomOpenTarget` payload includes the selected resource id, kind, title, location, result row, matched field, matched context tag/location prefix/resource relation label/note, score, match summary, and optional anchor. The `PinloomLibraryRootTarget` payload includes root id, path, display name, enabled/pinned state, last-indexed time, and root row. The `PinloomRelatedTarget` payload includes relation label/note, direction, and the related resource target.

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
- Tests: passed for host search control, embedded chrome options, result count/navigation/direct-selection/result-snapshot row metadata/match summaries/result-snapshot/status/library-root change notifications, current-target observation, resource-id open-target inspection and activation, current-related-target inspection, host-triggered current activation, host-triggered library-root add/remove, enablement, pinning, and root-by-id indexing, indexing controls with completion notifications/latest-result queries, required filters, tag/location/relation context ranking, relation-label-scoped context ranking, atomic host-context snapshots, host activation interception, and standalone fallback behavior (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Obsidian-Friendly Indexing MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for frontmatter aliases/tags, inline tags, wikilink aliases, wikilink line anchors/relations, Markdown body extraction, and block references (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

PDF Navigation MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Beacon/Location Indexing MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for unified text file classification, TODO/FIXME/NOTE line anchors, URL/error/warning/section/marker/simple symbol-like beacons, CMake project/target/package/test beacons, Makefile target beacons, Dockerfile stage/base/copy beacons, GitHub Actions workflow/job/step/action/run beacons, GitLab CI stage/job/needs/script beacons, compile_commands.json line anchors and `compiles` relations, and line-anchor UI display (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Manual Anchors And Relationships MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for related-resource persistence, indexed Markdown link relations, compact relationship summaries, host related-target inspection by selection and resource id, host relation editing, manual anchor/alias editing UI, and resource-id-based host manual editing (`pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Ranking And Recall MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for resource usage persistence, anchor usage persistence, exact-match ranking, pinned-resource ranking, pinned-root ranking, host tag/location/relation context ranking, context tooltip and host match-summary details, usage preservation across resource updates, activation recording, anchor activation recording, pin/unpin UI, and resource-id-based host pinning (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_widget_smoke_test`)

Integration And Source Refinement MVP validation:

- Configure: passed
- Build: passed
- Additional source-link validation: passed for plain-text URL list source line anchors and `links-to` relations (`pinloom_directory_source_test`)
- Tests: passed for Markdown body content extraction/search, Markdown task line anchors, local relative Markdown link aliases/anchors/relations, Markdown inline and reference-style external link URL resources with source line anchors and `links-to` relations, plain-text URL list resources, HAR/http archive entry resources, WARC response resources with source line anchors, iCalendar event URL resources with source line anchors and `links-to` relations, email message header/content indexing plus body URL resources with source line anchors and `links-to` relations, JSON/JSONL URL resources with source line anchors and `links-to` relations, YAML/TOML/INI/config URL resources with source line anchors and `links-to` relations, CSV/TSV URL-column resources with source row anchors and `links-to` relations, OPML link/feed indexing with source line anchors and `links-to` relations, RSS/Atom feed entry resources with source line anchors and `links-to` relations, sitemap URL resources with source line anchors and `links-to` relations, robots.txt sitemap resources, plain-text file content indexing, action line anchors, config key/section/path anchors, package manifest dependency anchors, CSV/TSV column anchors, local web shortcut indexing including desktop link shortcuts and URL/title line anchors, browser bookmark export HTML source line anchors and relations, native browser bookmark JSON source line anchors and relations, XBEL bookmark XML source line anchors and relations, Chromium/Edge-style History SQLite indexing, Firefox places.sqlite indexing with bookmark metadata, URL fragment anchors, URL fragment fallback opening, local HTML outbound link source line anchors, local HTML and MHTML/MHT page content/link extraction/search, optional remote HTML fetch for web shortcuts, lightweight PDF text extraction/search for uncompressed streams including `TJ` split glyph runs, FlateDecode, ASCIIHexDecode, RunLengthDecode, LZWDecode, literal octal escapes, UTF-16 BOM strings, ToUnicode CMaps, and ordered filter chains, URL activation through host interception, PDF outline/bookmark direct, named, and indirect named destination page anchors, PDF URI link annotation URL resources, PDF annotation region anchors, and PDF region open-target preservation (`pinloom_directory_source_test`, `pinloom_widget_smoke_test`)
