# Pinloom

Pinloom is a standalone Qt application for locating personal and engineering materials: files, folders, PDFs, text beacons, web pages, notes, tags, aliases, anchors, and jump relationships.

It is not an Obsidian add-on and is not a ZeroSlack-private feature. Obsidian vaults are one library source. ZeroSlack embedding is a later UI host target.

## Product Boundary

Pinloom is a beacon/location indexer and jump layer. It is not a code intelligence engine, a multilingual IDE, or a language semantic analyzer.

- Small files whose content sniffs as text are treated uniformly regardless of extension and feed the same text beacon model. Directory indexing keeps `.md/.markdown` on the ordinary file resource path; legacy Markdown resource kind inputs and stored `markdown` rows normalize to ordinary file resources.
- Pinloom extracts searchable positions: line text, headings/sections, TODO/FIXME/NOTE, URLs, errors/warnings, and explicit `MARKER`/`ANCHOR`/`BOOKMARK` aliases. Marker anchors are surfaced as markers, not code symbols. It does not build ASTs or claim programming-language support.
- Special file readers are reserved for formats that need them, such as PDF, SQLite databases, Office/Visio documents, content-verified web capture formats such as MHTML/HAR/WARC, and scanned/OCR material. Their purpose is still beacon and location extraction. Archive-like package containers (compressed packages, split archive volumes, legacy/platform-specific archives, installable package containers including Windows Installer/MSU, Windows App package variants, OpenWrt/IPK packages, browser/electron/mobile extension packages, and disk-image/virtual-disk containers) remain ordinary path-only file targets. Office/Visio readers for `doc/docx`, `xls/xlsx`, `ppt/pptx`, and `vsd/vsdx` are special-reader targets: they may inspect compound/package internals, but must emit only document-level logical locations such as paragraphs, sheets/cells, slides, pages, shapes, links, comments, or notes. They must never expose `word/document.xml`, `xl/worksheets/...`, `visio/pages/...`, or other package-internal paths as jump targets. Until those readers exist, these files may fall back to path-only resources; that fallback is not the final product boundary. Compressed or packaged files that look like reader inputs inside, such as `.har.gz`, `.warc.gz`, `.sqlite.gz`, or `.mhtml.zip`, still stay path-only.
- When embedded in ZeroSlack, Pinloom provides paths, line/column locations when available, display text, beacons, and jump targets. ZeroSlack owns Verilog/SystemVerilog editing, HDL semantics, rendering, and the jump action.

## Special File Reader Phases

Dedicated readers are staged so each phase improves location extraction without expanding Pinloom into a knowledge-modeling system.

1. Existing reader consolidation: describe and test current PDF, header-verified browser SQLite, HTML, content-verified MHTML/HAR/WARC/iCalendar/OPML/RSS/Atom/sitemap XML, robots, and email readers as beacon/location extractors; document text-structure and tabular coverage as unified text beacon rules, not special readers.
2. Engineering/design readers: keep generic SQLite table/column/URL/sample-value beacons on header-verified `.db`/`.sqlite`/`.sqlite3` database resources; non-SQLite `.db` text remains on the unified text path. SQLite URL values remain line anchors rather than derived URL resources or inferred `links-to` relations. Add Office/Visio readers for `doc/docx`, `xls/xlsx`, `ppt/pptx`, and `vsd/vsdx` as logical document readers, not archive expanders. Ordinary archive-like containers remain path-only, including compressed/package-wrapped reader-looking files such as `.har.gz`, `.warc.gz`, `.sqlite.gz`, and `.mhtml.zip`.
3. Scanned/OCR readers: extract OCR text from scanned PDFs and image-heavy documents with page/region anchors, confidence diagnostics, and page-level fallback jumps.
4. Reader contract and quality layer: standardize limits, output fields, position types, partial extraction, unsupported/encrypted/too-large states, timeouts, cancellation, and diagnostics.
5. Experience and performance: add reader toggles, incremental indexing, failure UI, jump fallbacks, deduplication, ranking/noise tuning, and large-file budgets.

## Current MVP: Integration And Source Refinement

Implemented:

- Qt/CMake project skeleton.
- `pinloom_core` static library with resource, anchor, search, repository interfaces, an in-memory repository, and a SQLite/FTS5 schema draft.
- SQLite-backed `SqliteLibraryRepository` with versioned schema initialization.
- Persistent resources, tags, aliases, anchors, and FTS5 metadata search.
- `LibrarySource` and `IndexingService` abstractions.
- `DirectoryLibrarySource` for explicit normal-directory indexing.
- Directory indexing emits `.md/.markdown` as ordinary file resources on the unified text path, not as a separate Markdown indexing branch.
- Persistent library root management in SQLite with schema version 2.
- Saved root CRUD in `ILibraryRepository`, `InMemoryLibraryRepository`, and `SqliteLibraryRepository`.
- `IndexingService` refresh/rebuild support for saved enabled roots.
- Anchor-aware search results with optional matched anchors.
- SQLite schema version 3 with anchor FTS search.
- Heading and block-id line anchors for `.md`, `.markdown`, and content-sniffed text files, emitted as neutral text heading/block anchors.
- Text-convention beacons for YAML-style frontmatter aliases/tags in `.md/.markdown` and content-sniffed text files, inline tags, bracketed text links, bracket-link line anchors/relations, and block-reference beacons.
- Searchable text body extraction without indexing frontmatter metadata as body text.
- Checkbox lines as searchable text beacons for direct jumps to the source line.
- Local relative links and bracketed text links as searchable aliases, line anchors, and automatic `links-to` relations when the target is indexed.
- Inline external text links in `.md/.markdown` and content-sniffed text files as derived URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source text file.
- Reference-style external text links in `.md/.markdown` and content-sniffed text files as derived URL resources with source line anchors and `links-to` relations back to the source text file.
- Lightweight PDF metadata extraction and page-level anchors.
- PDF outline/bookmark titles indexed as searchable page anchors when direct, named, or indirect named destinations point at pages.
- Lightweight PDF annotation region extraction for searchable `PdfRegion` anchors.
- PDF URI link annotations indexed as URL resources with fragment anchors and `links-to` relations back to the PDF.
- Lightweight PDF text extraction from uncompressed, FlateDecode, ASCIIHexDecode, ASCII85Decode, RunLengthDecode, and LZWDecode text content streams, including `TJ` text arrays with split glyph runs, literal octal escapes, UTF-16 BOM strings, ToUnicode CMaps, and ordered filter chains, for searchable PDF content.
- Local web shortcut indexing for `.url`, `.webloc`, `.website`, and `.desktop` link files as URL resources with shortcut URL/title line anchors.
- URL fragment anchors for indexed web shortcuts.
- Standalone fallback opening preserves matched URL fragment anchors for web pages and local HTML pages.
- Local `.html` and `.htm` page indexing with title, canonical URL aliases, heading fragment anchors, extracted searchable content, outbound URL resources, source line anchors, and `links-to` relations back to the source page.
- Content-verified local `.mhtml` and `.mht` web capture reader indexing with title, canonical URL aliases, heading fragment anchors, extracted searchable content, outbound URL resources, and `links-to` relations back to the capture file; non-capture `.mhtml`/`.mht` text remains on the unified text path.
- Browser bookmark export HTML indexing as individual URL resources with host aliases, bookmark tags, URL fragment anchors, source line anchors, and `links-to` relations back to the export file.
- Chromium/Edge-style Bookmarks JSON indexing as individual URL resources with host aliases, folder aliases, bookmark tags, URL fragment anchors, source line anchors, and `links-to` relations back to the Bookmarks file.
- XBEL bookmark XML indexing as individual URL resources with host/folder aliases, bookmark tags, URL fragment anchors, source line anchors, and `links-to` relations back to the XBEL file.
- Header-verified Chromium/Edge-style browser History SQLite indexing as individual URL resources with host aliases, visit-count aliases, URL fragment anchors, and `links-to` relations back to the history database; non-SQLite browser database-looking text remains on the unified text path.
- Header-verified Firefox `places.sqlite` indexing as individual URL resources with bookmark titles, bookmark/folder aliases, visit-count aliases, URL fragment anchors, and `links-to` relations back to the places database; non-SQLite `places.sqlite` text remains on the unified text path.
- Generic SQLite database indexing for header-verified `.db`/`.sqlite`/`.sqlite3` files with table, column, URL, and sample-value beacons on the source database resource; non-SQLite `.db` text remains on the unified text path, and SQLite URL values remain database line anchors rather than derived URL resources or inferred `links-to` relations.
- Content-verified OPML subscription/link list indexing as individual URL resources with host aliases, feed aliases, folder aliases, feed tags, URL fragment anchors, source line anchors, and `links-to` relations back to the OPML file; non-OPML `.opml` text remains on the unified text path.
- Content-verified RSS and Atom feed XML indexing as individual URL resources with feed aliases, category tags, host aliases, URL fragment anchors, source line anchors, and `links-to` relations back to the feed file; non-feed `.rss`/`.atom`/`.xml` text remains on the unified text path.
- Content-verified sitemap XML indexing as individual URL resources with sitemap tags, host aliases, URL fragment anchors, source line anchors, and `links-to` relations back to the source file; non-sitemap `.xml` text remains on the unified text path.
- `robots.txt` Sitemap line indexing as individual URL resources with robots/sitemap tags, source line anchors, and `links-to` relations back to the source file.
- Content-verified HAR/http capture entry URL resource indexing with page-title aliases, HTTP method/status tags, URL fragment anchors, source line anchors, and `links-to` relations back to the capture file; non-HAR `.har` text remains on the unified text path.
- Content-verified WARC response URL resource indexing with extracted HTML content, host aliases, URL fragment anchors, source line anchors, and `links-to` relations back to the WARC file; non-WARC `.warc` text remains on the unified text path.
- Content-verified iCalendar `.ics`/`.ical` event indexing with event/time/location line anchors plus event URL resources, URL fragment anchors, and `links-to` relations back to the source calendar; non-calendar `.ics`/`.ical` text remains on the unified text path.
- Email `.eml` message indexing with subject/from/to/date line anchors plus body URL resources, URL fragment anchors, searchable message content, and `links-to` relations back to the source message.
- Bare URL indexing in `.md/.markdown` and content-sniffed text files as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source text file.
- CSV/TSV URL-column indexing as individual URL resources with host aliases, table aliases, category/tag aliases, fragment anchors, source row anchors, and `links-to` relations back to the source table.
- JSON/JSONL URL string indexing as individual URL resources with host/path aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- YAML/TOML/INI/config URL string indexing as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Optional remote HTML fetching for indexed web shortcuts, with fetched page titles, canonical URL aliases, heading fragment anchors, and extracted searchable content.
- Lightweight content indexing for small content-sniffed text, config, and tabular files, including TODO/FIXME/NOTE line anchors, text-structure key/section/path anchors, explicit JSON/TOML named-entry container line beacons, and CSV/TSV column anchors.
- Unified text beacon indexing for small content-sniffed text files, including code-looking and tool-script-looking suffixes, without language extension or shebang interpreter allowlists, including TODO/FIXME/NOTE, URL, error/warning, section-like, and explicit `MARKER`/`ANCHOR`/`BOOKMARK` line anchors.
- `.md/.markdown` directory-scan resources use the same ordinary file kind and text URL/beacon pipeline as other content-sniffed text files; legacy Markdown resource kind inputs and stored rows remain readable but normalize to ordinary file resources in SQLite and in-memory repositories.
- Text heading/block anchors use neutral `TextHeading`/`TextBlock` model values and `text_heading`/`text_block` storage names for new directory indexing; SQLite and in-memory repositories normalize legacy Markdown-named anchor inputs and stored rows to neutral text anchors.
- Shared legacy compatibility helpers keep Markdown-named resource kinds, filters, anchor inputs, and stored rows as aliases for ordinary file resources and neutral text anchors across repository implementations.
- Repository smoke coverage uses neutral file/text fixtures for ordinary behavior; Markdown-named resource and anchor inputs are tested only as legacy compatibility.
- Text named-entry line beacons use neutral `named entry ...` labels only for explicit JSON/TOML `entries`/`items`/`markers`/`beacons`/`anchors` containers; dependency-like keys remain ordinary text and do not become named-entry anchors.
- Neutral text snippet and marker anchor model/storage names; new writes use neutral `text_snippet`/`marker` storage while retaining read-only legacy `symbol_like`/`code_*` database compatibility.
- Shared path-only package-container guard: archive-like package containers (common compressed packages, split archive volumes, compound and shorthand tar suffixes, legacy/platform-specific archives, installable package containers including Windows Installer/MSU, Windows App package variants, OpenWrt/IPK packages, browser/electron/mobile extension packages, and disk-image/virtual-disk containers) remain permanent path-only targets. Guarded files carry `path-only` and `package-container` tags through indexing and are validated to have no scanned content, anchors, relations, derived package-inside resources, searchable package-internal path strings, or reader output from compressed/package-wrapped HAR/WARC/SQLite/MHTML-looking payloads. Current path-only handling for Office/Visio files is only a temporary fallback until logical document readers are implemented.
- Config-style text beacons layered after text content sniffing for neutral config entries/settings/blocks/references, rule-entry lines, container-style config block/input lines, workflow/pipeline configuration text beacons with `config ...` labels, and file-reference manifest line anchors for compile_commands.json with `file-reference` relations to referenced file paths. File-reference manifests leave command strings as ordinary searchable file text only, not beacon details, relations, or build semantics. These are line-pattern beacons for jumps, not build-system, container, or CI platform interpretation; rule names, command/call words, and container instruction words stay text triggers.
- Persistent manual and indexed related-resource links with host-facing relation editing and a compact relationship summary in the locator panel. Manual relation labels are user-authored jump context, not inferred semantic relationships.
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

- Full PDF text/content extraction for remaining unsupported filters, complex encodings, and OCR; broader web source support beyond local HTML/MHTML/WARC, desktop shortcuts, bookmarks/XBEL/history/places with bookmark metadata, OPML, feeds, sitemaps/robots.txt hints, HAR/http captures, iCalendar/email files, content-sniffed text URLs, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.
- Full ZeroSlack dock/global-control integration.

Next:

- Full ZeroSlack dock/global-control integration.
- Special-file reader phase 1 consolidation and reader contract cleanup.
- Fuller PDF text extraction for remaining unsupported filters/encodings, OCR, and broader web source support beyond local HTML/MHTML/WARC, desktop shortcuts, bookmarks/XBEL/history/places with bookmark metadata, OPML, feeds, sitemaps/robots.txt hints, HAR/http captures, iCalendar/email files, content-sniffed text URLs, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.

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

Precise Text Beacon Jumps MVP validation:

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
- Tests: passed for host search control, embedded chrome options, result count/navigation/direct-selection/result-snapshot row metadata/match summaries/result-snapshot/status/library-root change notifications, non-activating path/location/anchor open-target observation, resource-id open-target inspection and activation, current-related-target inspection, host-triggered current activation, host-triggered library-root add/remove, enablement, pinning, and root-by-id indexing, indexing controls with completion notifications/latest-result queries, required filters, tag/location/relation context ranking, relation-label-scoped context ranking, atomic host-context snapshots, host activation interception, and standalone fallback behavior (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Text Convention Beacon MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for frontmatter aliases/tags in `.md/.markdown` and content-sniffed text files, inline tags, bracketed text-link aliases, bracket-link line anchors/relations, body text extraction from text files, and block-reference beacons (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

PDF Navigation MVP validation:

- Configure: passed
- Build: passed
- Tests: passed (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Beacon/Location Indexing MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for content-sniffed unified text file classification, neutral text/log/config/no-extension/code-looking/tool-script beacon fixtures, ambiguous key/certificate-like, non-SQLite `.db`, non-SQLite browser database-looking text suffixes/names, non-capture `.mhtml`/`.mht`, non-calendar `.ics`/`.ical`, and non-capture `.har`/`.warc` text staying out of package-container/special-reader guards, unknown-extension text beacons, binary-like NUL rejection, path-only archive-like package containers without scanned content, anchors, relations, derived package-inside resources, searchable package-internal path strings, or compressed/package-wrapped reader-looking payload output, temporary Office/Visio path-only fallback coverage before logical readers exist, TODO/FIXME/NOTE line anchors, URL/error/warning/section/explicit marker-alias beacons, config-style text line beacons, rule-entry and neutral container-style config block/input line beacons, neutral workflow/pipeline configuration text line beacons, file-reference manifest line anchors for compile_commands.json and `file-reference` relations, and line-anchor UI display (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Manual Anchors And Relationships MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for related-resource persistence, indexed text link relations from local link beacons, compact relationship summaries, host related-target inspection by selection and resource id, host relation editing, manual anchor/alias editing UI, and resource-id-based host manual editing (`pinloom_sqlite_repository_test`, `pinloom_directory_source_test`, `pinloom_widget_smoke_test`)

Ranking And Recall MVP validation:

- Configure: passed
- Build: passed
- Tests: passed for resource usage persistence, anchor usage persistence, exact-match ranking, pinned-resource ranking, pinned-root ranking, host tag/location/relation context ranking, context tooltip and host match-summary details, usage preservation across resource updates, activation recording, anchor activation recording, pin/unpin UI, and resource-id-based host pinning (`pinloom_core_smoke_test`, `pinloom_sqlite_repository_test`, `pinloom_widget_smoke_test`)

Integration And Source Refinement MVP validation:

- Configure: passed
- Build: passed
- Additional source-link validation: passed for bare URL source line anchors and `links-to` relations in `.md/.markdown` and content-sniffed text files (`pinloom_directory_source_test`)
- Tests: passed for body text extraction/search from text files, checkbox line beacons, local relative link aliases/anchors/relations, inline and reference-style external text-link URL resources with source line anchors and `links-to` relations across `.md/.markdown` and content-sniffed text files, bare URL resources across `.md/.markdown` and content-sniffed text files, content-verified HAR/http capture entry URL resources, content-verified WARC response URL resources with source line anchors, non-capture `.har`/`.warc` text staying on unified text URL/beacon extraction, content-verified iCalendar event URL resources with source line anchors and `links-to` relations, non-calendar `.ics`/`.ical` text staying on unified text URL/beacon extraction, email message header/content indexing plus body URL resources with source line anchors and `links-to` relations, JSON/JSONL URL resources with source line anchors and `links-to` relations, YAML/TOML/INI/config URL resources with source line anchors and `links-to` relations, CSV/TSV URL-column resources with source row anchors and `links-to` relations, content-verified OPML link/feed indexing with source line anchors and `links-to` relations, non-OPML `.opml` text staying on unified text URL/beacon extraction, content-verified RSS/Atom feed entry resources with source line anchors and `links-to` relations, non-feed `.rss`/`.atom`/`.xml` text staying on unified text URL/beacon extraction, content-verified sitemap URL resources with source line anchors and `links-to` relations, non-sitemap `.xml` text staying on unified text URL/beacon extraction, robots.txt Sitemap line resources, content-sniffed text content indexing, TODO/FIXME/NOTE text beacon anchors, text-structure key/section/path anchors, explicit JSON/TOML named-entry container line beacons, CSV/TSV column anchors, local web shortcut indexing including desktop link shortcuts and URL/title line anchors, browser bookmark export HTML source line anchors and relations, native browser bookmark JSON source line anchors and relations, XBEL bookmark XML source line anchors and relations, header-verified Chromium/Edge-style History SQLite indexing, header-verified Firefox places.sqlite indexing with bookmark metadata, non-SQLite browser database-looking text staying on unified text beacons, URL fragment anchors, URL fragment fallback opening, local HTML outbound link source line anchors, local HTML page and content-verified MHTML/MHT web capture content/link extraction/search, non-capture `.mhtml`/`.mht` text staying on unified text URL/beacon extraction, optional remote HTML fetch for web shortcuts, lightweight PDF text extraction/search for uncompressed streams including `TJ` split glyph runs, FlateDecode, ASCIIHexDecode, RunLengthDecode, LZWDecode, literal octal escapes, UTF-16 BOM strings, ToUnicode CMaps, and ordered filter chains, URL activation through host interception, PDF outline/bookmark direct, named, and indirect named destination page anchors, PDF URI link annotation URL resources, PDF annotation region anchors, and PDF region open-target preservation (`pinloom_directory_source_test`, `pinloom_widget_smoke_test`)
