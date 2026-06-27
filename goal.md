# Pinloom Goal

## Long-Term Goal

Pinloom is a fast beacon and location indexer for personal and engineering knowledge. It helps users jump to the right material, beacon, note, file line, PDF page, folder, web page, alias, tag, or related resource without becoming a full-disk search tool.

Pinloom's long-term product shape is a personal knowledge locator and jump layer. It should answer "where do I need to go next?" better than "which files contain this string?" Search quality is measured by how quickly a user lands on the right source, beacon, page, file line, or related note inside a curated library.

## Architecture Principles

- Standalone first: Pinloom must run as a complete Qt application.
- Embeddable UI: reusable widgets must later fit into ZeroSlack as a dock/global-control panel.
- Core independence: `pinloom_core` must not depend on ZeroSlack UI or app-specific host behavior.
- Source neutrality: Obsidian vaults, normal folders, PDFs, text files, notes, and manual anchors are all library sources.
- Unified text treatment: small files whose content sniffs as text are indexed as text, regardless of extension, not as language-specific resources.
- No language semantics: Pinloom extracts searchable beacons and locations only; it does not build ASTs, infer program meaning, or claim support for programming languages.
- Special readers only for special files: PDF, SQLite databases, scanned/OCR material, and web capture formats such as MHTML/HAR/WARC may have dedicated readers, but their job is still beacon and location extraction. Archive-like package containers (compressed packages, split archive volumes, legacy/platform-specific archives, installable package containers including Windows Installer/MSU packages, browser/electron/mobile extension packages, and disk-image containers) plus compound/package document-design containers such as doc/docx, xls/xlsx, ppt/pptx, vsd/vsdx, odt/ods/odp, and epub remain ordinary path-only file targets: they are never expanded, text-scanned, modeled as archive entries, or exposed through package-internal paths.
- ZeroSlack boundary: when embedded in ZeroSlack, Pinloom provides paths, lines, columns when available, display text, beacons, and jump targets; ZeroSlack owns HDL editing, display, and jump execution.
- Text-convention friendliness: tags, aliases, headings, bracketed text links, and block ids should become beacons without making Pinloom an Obsidian add-on or Markdown language layer.
- Precise anchors: search results should be able to land on PDF pages/regions, file lines, heading/block line anchors in text files, URLs, and manual targets.
- Locator search: optimize for curated library positioning and fast jumps, not whole-disk crawling.

## Long-Term Roadmap

- ZeroSlack embedding boundary: make `pinloom_widgets` usable by standalone Pinloom and ZeroSlack without transferring ownership of core state to either host.
- Text-convention beacons: extract frontmatter aliases, inline tags, bracketed text links, and block-reference beacons as text positions while keeping the same locator model outside Obsidian.
- PDF navigation: index PDF metadata and pages first, then add page and region jump targets.
- Beacon/location indexing: index text files by paths, line beacons, user markers, URLs, warnings/errors, section-like lines, and project-relevant tags without language parsing.
- Special-file reader roadmap: grow dedicated readers in phases for formats that are not simple text, while keeping every reader limited to beacon and location extraction.
- Manual anchors and relationships: let users create aliases, manual anchors, and related-resource links from the UI.
- Ranking and recall: combine match type, recency, usage frequency, pinned resources, tags, and active project context.
- Packaging and reliability: make standalone launch, deployment, database upgrades, and diagnostics boringly dependable.

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

### Directory Source Indexing MVP

Completed:

- Source/indexing abstractions that produce resources without depending on UI.
- Explicit normal-directory source for user-selected roots.
- Directory indexing for folders, text files, PDFs, and generic files.
- Idempotent indexing through stable file-location resource ids.
- Directory source tests proving root-scoped scanning and repeated indexing behavior.

### Usable Locator UI MVP

Completed:

- Standalone app opens and initializes the default SQLite database.
- Reusable widget accepts repository injection instead of owning fixed in-memory data.
- UI can add explicit folder roots, refresh directory indexing, search repository results, and open selected paths.
- Widget smoke test validates repository injection and search display wiring.

### Library Root Management MVP

Completed:

- Persistent library root model and SQLite schema v2.
- Repository root CRUD across SQLite and in-memory implementations.
- Saved-root indexing, refresh-all, and rebuild-all service flows.
- Reusable UI for managing saved folders and rebuilding the resource index.
- Tests for root persistence, v1-to-v2 upgrade, saved-root indexing, rebuilds, and widget root loading.

### Precise Text Beacon Jumps MVP

Completed:

- Search results can carry a matched anchor.
- SQLite schema v3 indexes anchors for heading and block searches.
- Directory indexing extracts heading and block-id line anchors from text files.
- UI shows anchor-aware results and opens line anchors in a built-in read-only text preview.
- Tests cover text heading/block anchor extraction, anchor search, v2-to-v3 upgrade, anchor-aware UI display, and text preview loading.

### Search Results Cleanup MVP

Completed:

- Search result labels no longer expose internal `fts` fields.
- Anchor, title, filename, alias, and tag matches rank ahead of full-path matches.
- Result items show readable two-line text and retain full-path tooltips.
- Tests cover result ordering, filename matching, anchor display, and tooltip behavior.

### ZeroSlack Embedding Boundary MVP

Completed:

- Keep `pinloom_core` host-neutral.
- Expose `pinloom_widgets` APIs that let a host seed search text, focus the locator, and intercept selected result activation.
- Let embedding hosts hide standalone management/editing chrome for compact dock/global-control embedding.
- Let embedding hosts inspect path/location/anchor open-target payloads and observe selection changes without triggering activation.
- Let embedding hosts inspect the current result target list without parsing Qt item text.
- Let embedding hosts inspect related targets for the current selection without parsing UI text.
- Let embedding hosts inspect result count, navigate result selection, and directly select results by row or resource id through API for command palette flows.
- Let embedding hosts observe result-count changes after search/filter refreshes.
- Let embedding hosts observe current result target snapshots after search/filter refreshes.
- Let embedding hosts inspect and observe status text without reading UI labels.
- Let embedding hosts inspect, select, and observe library root snapshots and selected-root changes without parsing UI text.
- Let embedding hosts add and remove library roots through API without invoking standalone file dialogs.
- Let embedding hosts pin and unpin library roots through API while preserving standalone pin behavior.
- Let embedding hosts enable and disable library roots through API for indexing-scope control.
- Let embedding hosts activate the current result through API for command palette or global-shortcut flows.
- Let embedding hosts control the optional remote HTML fetch setting used during indexing.
- Let embedding hosts trigger selected-root, root-by-id, all-root, and rebuild indexing flows with structured results.
- Let embedding hosts observe completed indexing results and query the latest structured indexing result without reading UI text.
- Let embedding hosts add aliases and manual anchors by resource id without driving result-list selection.
- Let embedding hosts pin and unpin resources by id without driving result-list selection.
- Let embedding hosts create, update, and remove resource relations by id without driving result-list selection.
- Let embedding hosts inspect open-target payloads by resource id without driving search or result-list selection.
- Let embedding hosts activate resources by id without driving search or result-list selection.
- Let embedding hosts inspect related targets by resource id without driving result-list selection.
- Let embedding hosts hard-filter searches by required resource kinds, tags, and location prefixes while using context tags, prefixes, related resource ids, and optional relation-label scopes for ranking.
- Let embedding hosts apply active search/filter/ranking context as one snapshot for project/document changes.
- Include resource kind, title, result row, matched field, matched context signals, matched relation label/note, score, match summary, and anchor details in host activation payloads.
- Include relation label, note, direction, and related resource target details in host related-target payloads.
- Keep standalone behavior as the default fallback.
- Document which responsibilities belong to Pinloom and which belong to the embedding host.

Remaining follow-up:

- Wire the boundary into the actual ZeroSlack dock/global-control host.

### Text Convention Beacon MVP

Completed:

- Extract YAML-style frontmatter aliases and tags as text beacons in `.md/.markdown` and content-sniffed text files.
- Extract inline `#tags`, bracketed text links such as `[[...]]`, and block-reference beacons as line anchors.
- Extract searchable body text while excluding frontmatter metadata from body text.
- Extract task checkbox lines as searchable file-line anchors.
- Extract local relative links as searchable aliases, file-line anchors, and indexed `links-to` relations.
- Extract bracketed text links as searchable file-line anchors and indexed `links-to` relations.
- Treat `.md/.markdown` inputs from directory scanning as ordinary file resources on the unified text path; Markdown resource kind inputs and stored `markdown` rows are compatibility surface and normalize to ordinary file resources, not a new indexing branch.
- Emit text heading/block anchors with neutral text anchor types; SQLite and in-memory repositories normalize Markdown-named anchor inputs and `markdown_*` storage rows to neutral text anchors on write/read.
- Keep Markdown-named resource and anchor compatibility centralized in one shared normalization layer so repository implementations cannot drift back toward a format-specific branch.
- Keep SQLite and in-memory repository coverage on neutral file/text fixtures, with Markdown-named resource and anchor inputs reserved for explicit legacy compatibility tests.
- Preserve the same text-beacon behavior for non-Obsidian text folders.
- Add focused tests around mixed plain text and note-style text-convention inputs.

### PDF Navigation MVP

Completed:

- Store PDF page anchors.
- Store PDF outline/bookmark titles as searchable page anchors when direct, named, or indirect named destinations point at pages.
- Add basic PDF metadata extraction.
- Open search hits at the intended page when the platform viewer supports it.
- Extract PDF annotation rectangles as region anchors.
- Index PDF URI link annotations as URL resources with fragment anchors and `links-to` relations back to the PDF.

### Beacon/Location Indexing MVP

Completed:

- Treat content-sniffed text files, including HDL/C++/JavaScript/Tcl/XDC/script-looking suffixes, as normal file resources with searchable content instead of code-language resources, extension lists, or shebang interpreter allowlists.
- Keep note-style text extensions on the same normal file resource path rather than emitting format-specific resource kinds from directory scans.
- Extract neutral text beacons for TODO/FIXME/NOTE lines, URLs, errors/warnings, section-like lines, and explicit `MARKER`/`ANCHOR`/`BOOKMARK` aliases, with marker anchors surfaced as markers rather than code symbols.
- Keep config-style text line beacons for content-sniffed jumps, with neutral `config entry/setting/block/reference ...` labels plus format-trigger and reference implementation naming rather than build-system command or call-kind semantics.
- Keep rule-entry and container-style text line beacons for content-sniffed jumps, with neutral `rule ...` and `config block/input ...` labels and format-trigger naming; rule names and container instruction words remain text triggers, not build-tool or container-platform semantics.
- Keep workflow/pipeline configuration text line beacons for workflow/block/stage/step/uses/run/script/needs jumps, with neutral `config ...` labels plus format-trigger and block/label implementation state naming rather than vendor-specific CI platform interpretation.
- Keep explicit JSON/TOML named-entry container line beacons with neutral `named entry ...` labels; dependency-like keys remain ordinary text and do not become named-entry anchors.
- Add file-reference manifest line anchors for compile_commands.json and `file-reference` relations to referenced file paths; command strings remain ordinary searchable file text only, not beacon details, relations, or build semantics.
- Remove automatic code classification and language-specific symbol/test/import parsing from the product path, without maintaining a language support matrix.
- Use neutral model/storage names for text snippets and marker anchors; new writes use neutral `text_snippet`/`marker` storage while retaining read-only legacy `symbol_like`/`code_*` database compatibility.
- Use neutral text heading/block anchor model and storage names for new directory indexing while normalizing legacy Markdown-named anchor inputs and stored rows.
- Centralize legacy Markdown resource/anchor aliases behind shared compatibility helpers, preserving old input/database readability while keeping new writes on ordinary file and neutral text-anchor names.
- Rank exact beacon and filename matches ahead of broad path matches.

Remaining follow-up:

- Improve beacon precision and noise control without turning Pinloom into a language parser or IDE.

### Manual Anchors And Relationships MVP

Completed:

- Persist related-resource links.
- Persist indexed `links-to` relations discovered from local text link beacons.
- Surface relationships in compact result details without turning the main result list into a graph browser.
- Treat manual relation labels as user-authored jump context, not inferred semantic relationships.
- Expose current related targets through the host-facing panel API.
- Expose related-target lookup by explicit resource id through the host-facing panel API.
- Expose host APIs for creating, updating, and removing resource relations.
- Add UI affordances for creating manual aliases and anchors.
- Expose resource-id-based host APIs for adding manual aliases and anchors.

### Ranking And Recall MVP

Completed:

- Track recently opened resources and frequently used resource activations.
- Track recently opened anchors and frequently used precise jumps.
- Support pinned resources.
- Expose resource-id-based host API for pinning and unpinning resources.
- Support pinned library roots as project-level recall signals.
- Support host-required kind/tag/location filters alongside host context ranking.
- Support host-scoped relation labels for relation-aware context ranking.
- Apply recall signals as small ranking boosts without letting weak path matches outrank stronger match types.
- Boost exact title, filename, alias, tag, content, path, and anchor matches within their match type.
- Add project/context weighting, including relation-aware active-resource weighting, while keeping ranking understandable.
- Show matched fields, anchors, and host context matches, including matched relation notes, in result tooltips and host-facing match summaries.

### Integration And Source Refinement MVP

In progress:

Completed:

- Index local web shortcut files, including desktop link shortcuts, as URL resources without requiring network access, including shortcut URL/title line anchors.
- Preserve URL fragments as searchable URL anchors.
- Open URL resources through host interception or the standalone fallback URL opener.
- Preserve matched URL fragment anchors when standalone fallback opens web pages or local HTML pages.
- Extract PDF annotation rectangles as searchable region anchors.
- Index PDF URI link annotations as individual URL resources with `links-to` relations back to the source PDF.
- Extract PDF outline/bookmark direct, named, and indirect named destinations as searchable page anchors.
- Extract searchable PDF text from uncompressed text content streams, including `TJ` text arrays with split glyph runs.
- Extract searchable PDF text from FlateDecode text content streams.
- Extract searchable PDF text from ASCIIHexDecode text content streams and literal octal escapes.
- Extract searchable PDF text from ASCII85Decode text content streams.
- Extract searchable PDF text from RunLengthDecode text content streams.
- Extract searchable PDF text from LZWDecode text content streams.
- Apply ordered PDF stream filter chains for supported filters.
- Decode UTF-16 BOM PDF strings in titles and content streams.
- Decode basic PDF ToUnicode CMaps for font-encoded content streams.
- Extract searchable body text from text files.
- Extract task checkbox lines as searchable file-line anchors.
- Extract local relative links as searchable aliases, file-line anchors, and indexed `links-to` relations.
- Index inline external text links from `.md/.markdown` and content-sniffed text files as derived URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source text file.
- Index reference-style external text links from `.md/.markdown` and content-sniffed text files as derived URL resources with source line anchors and `links-to` relations back to the source text file.
- Index local HTML pages with searchable extracted content, canonical URL aliases, and heading fragment anchors.
- Index MHTML/MHT web capture reader outputs with searchable HTML content, canonical URL aliases, and heading fragment anchors.
- Index local HTML outbound links as derived URL resources with source line anchors and `links-to` relations back to the source page.
- Index MHTML/MHT outbound links as derived URL resources with `links-to` relations back to the source capture file.
- Index browser bookmark export HTML as individual URL resources with searchable host aliases, fragment anchors, source line anchors, and `links-to` relations back to the export file.
- Index Chromium/Edge-style Bookmarks JSON as individual URL resources with searchable host/folder aliases, fragment anchors, source line anchors, and `links-to` relations back to the Bookmarks file.
- Index XBEL bookmark XML as individual URL resources with searchable host/folder aliases, fragment anchors, source line anchors, and `links-to` relations back to the XBEL file.
- Index Chromium/Edge-style browser History SQLite files as individual URL resources with visit-count aliases, fragment anchors, and `links-to` relations back to the history database.
- Index Firefox `places.sqlite` files as individual URL resources with bookmark titles, bookmark/folder aliases, visit-count aliases, fragment anchors, and `links-to` relations back to the places database.
- Index OPML subscription/link lists as individual URL resources with searchable host/feed/folder aliases, fragment anchors, source line anchors, and `links-to` relations back to the OPML file.
- Index RSS and Atom feed XML entries as individual URL resources with searchable feed aliases, category tags, fragment anchors, source line anchors, and `links-to` relations back to the feed file.
- Index sitemap XML URLs as individual URL resources with searchable sitemap tags, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Index `robots.txt` Sitemap lines as individual URL resources with searchable robots/sitemap tags, source line anchors, and `links-to` relations back to the source file.
- Index HAR/http capture entry URL resources with page-title aliases, HTTP method/status tags, fragment anchors, source line anchors, and `links-to` relations back to the capture file.
- Index WARC response URL resources with extracted HTML content, fragment anchors, source line anchors, and `links-to` relations back to the WARC file.
- Index iCalendar event files with event/time/location anchors plus event URL resources, fragment anchors, and `links-to` relations back to the source calendar.
- Index email `.eml` message files with subject/from/to/date line anchors, searchable message content, body URL resources, fragment anchors, and `links-to` relations back to the source message.
- Index bare URLs found in `.md/.markdown` and small content-sniffed text files as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Index CSV/TSV URL columns as individual URL resources with host aliases, table aliases, category/tag aliases, fragment anchors, source row anchors, and `links-to` relations back to the source table.
- Index JSON/JSONL URL strings as individual URL resources with path aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Index YAML/TOML/INI/config URL strings as individual URL resources with fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Optionally fetch remote HTML for indexed web shortcuts and reuse the same title, content, canonical URL, and heading-anchor extraction.
- Index small content-sniffed text, config, and tabular files as searchable File content with TODO/FIXME/NOTE line anchors, text-structure key/section/path anchors, explicit JSON/TOML named-entry container line beacons, and CSV/TSV column anchors.

Remaining:

- Wire the reusable panel into the actual ZeroSlack dock/global-control host.
- Add fuller PDF content extraction for remaining unsupported filters, complex encodings, and OCR, plus broader web source support beyond local HTML/MHTML/WARC, shortcuts, bookmarks/XBEL/history/places with bookmark metadata, OPML, feeds, sitemaps/robots.txt hints, HAR/http captures, iCalendar/email files, content-sniffed text URLs, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.
- Improve generic text beacon extraction and special-file readers without pursuing richer language parsing.

### Special-File Reader Roadmap

Reader boundary:

- Special readers exist only for formats that need format-aware extraction.
- Reader output is still content, aliases, tags, relations, diagnostics, and anchors with positions.
- Readers must not become document understanding, language semantics, or knowledge-modeling engines.

Phases:

1. Existing reader consolidation: keep PDF, browser SQLite, HTML/MHTML, HAR/WARC, OPML/RSS, sitemap/robots, iCalendar, and email described and tested as beacon/location readers; keep JSON/YAML/TOML/INI and CSV/TSV coverage described as text beacon rules, not special readers.
2. Engineering and design special readers: generic SQLite table/column/URL/sample-value beacons are in place; SQLite URL values remain database line anchors rather than derived URL resources or inferred `links-to` relations. Archive-like package containers and compound/package document-design containers such as doc/docx, xls/xlsx, ppt/pptx, vsd/vsdx, odt/ods/odp, and epub are not reader targets; they remain ordinary path-only package-container file resources.
3. Scanned/OCR readers: add OCR text, page/region anchors, confidence diagnostics, and fallback page-level jumps for scanned PDFs and image-heavy documents.
4. Reader contract and quality layer: standardize reader input limits, output schema, position types, diagnostics, partial extraction, unsupported/encrypted/too-large states, and timeout behavior.
5. Experience and performance: add reader toggles, incremental indexing, cancellation, failure UI, jump fallback behavior, deduplication, and ranking/noise tuning.

Current boundary enforcement:

- A shared path-only package-container guard keeps archive-like package containers (common compressed packages, split archive volumes, compound and shorthand tar suffixes, legacy/platform-specific archives, installable package containers including Windows Installer/MSU packages, browser/electron/mobile extension packages, and disk-image containers) plus compound/package document-design containers out of expansion, generic text scanning, and derived-resource indexing. These are permanent path-only file targets, tagged `path-only` and `package-container`; current validation requires those tags to survive indexing while package resources have no scanned content, anchors, relations, derived package-inside resources, or searchable package-internal path strings.
