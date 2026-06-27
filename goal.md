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
- Special readers only for special files: PDF, Office documents, Visio, SQLite databases, scanned/OCR material, and web capture formats such as MHTML/HAR/WARC may have dedicated readers, but their job is still beacon and location extraction. Compressed packages such as zip/jar/tar/gz/br/bz2/xz/zst/7z/rar, app/archive packages, and compound tar packages are never expanded or text-scanned; they remain ordinary path-only file targets. Office/Visio package containers stay path-only until a document reader can emit real document positions.
- ZeroSlack boundary: when embedded in ZeroSlack, Pinloom provides paths, lines, columns when available, display text, beacons, and jump targets; ZeroSlack owns HDL editing, display, and jump execution.
- Obsidian friendliness: markdown tags, aliases, headings, wikilinks, and block ids should be indexed without making Pinloom an Obsidian add-on.
- Precise anchors: search results should be able to land on PDF pages/regions, file lines, markdown headings/blocks, URLs, and manual targets.
- Locator search: optimize for curated library positioning and fast jumps, not whole-disk crawling.

## Long-Term Roadmap

- ZeroSlack embedding boundary: make `pinloom_widgets` usable by standalone Pinloom and ZeroSlack without transferring ownership of core state to either host.
- Obsidian-friendly indexing: extract frontmatter aliases, inline tags, wikilinks, and block-reference beacons while keeping Markdown support useful outside Obsidian.
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
- Directory indexing for folders, markdown files, PDFs, and generic files.
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

### Precise Text and Markdown Jumps MVP

Completed:

- Search results can carry a matched anchor.
- SQLite schema v3 indexes anchors for heading and block searches.
- Directory indexing extracts Markdown headings and block ids with line numbers.
- UI shows anchor-aware results and opens line anchors in a built-in read-only text preview.
- Tests cover Markdown anchor extraction, anchor search, v2-to-v3 upgrade, anchor-aware UI display, and text preview loading.

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
- Let embedding hosts inspect the current selected open target and observe selection changes without triggering activation.
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

### Obsidian-Friendly Indexing MVP

Completed:

- Extract YAML frontmatter aliases and tags.
- Extract inline `#tags`, `[[wikilinks]]`, and block-reference beacons.
- Extract searchable Markdown body content while excluding frontmatter metadata from body text.
- Extract Markdown task checkbox lines as searchable file-line anchors.
- Extract local relative Markdown links as searchable aliases, file-line anchors, and indexed `links-to` relations.
- Extract Obsidian wikilinks as searchable file-line anchors and indexed `links-to` relations.
- Preserve normal Markdown behavior for non-Obsidian folders.
- Add focused tests around mixed plain-Markdown and Obsidian vault inputs.

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

- Treat content-sniffed text files as normal file resources with searchable content instead of code-language resources, extension lists, or shebang interpreter allowlists.
- Extract neutral text beacons for TODO/FIXME/NOTE lines, URLs, errors/warnings, section-like lines, user markers, and explicit symbol-like marker lines.
- Keep directive-style build configuration line beacons for content-sniffed text jumps, with neutral `directive ...` labels rather than build-language semantics.
- Keep rule-target and container-recipe line beacons for content-sniffed text jumps, with neutral `rule ...` and `container ...` labels rather than Make/Docker language support.
- Keep CI configuration workflow/job/stage/step/uses/run/script/needs line beacons for jumps, with neutral `ci ...` labels rather than vendor-specific CI language support.
- Keep JSON/TOML/requirements-style dependency line beacons as text beacons, not package-manager or language dependency analysis.
- Add compile_commands.json line anchors and `build-input` relations from build databases to referenced file paths.
- Remove automatic code classification and language-specific symbol/test/import parsing from the product path, without maintaining a language support matrix.
- Use neutral model/storage names for text snippets and symbol-like anchors while retaining legacy `code_*` database read compatibility.
- Rank exact beacon and filename matches ahead of broad path matches.

Remaining follow-up:

- Improve beacon precision and noise control without turning Pinloom into a language parser or IDE.

### Manual Anchors And Relationships MVP

Completed:

- Persist related-resource links.
- Persist indexed `links-to` relations discovered from local Markdown links.
- Surface relationships in compact result details without turning the main result list into a graph browser.
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
- Extract searchable Markdown body content.
- Extract Markdown task checkbox lines as searchable file-line anchors.
- Extract local relative Markdown links as searchable aliases, file-line anchors, and indexed `links-to` relations.
- Index Markdown external links as derived URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source note.
- Index Markdown reference-style external links as derived URL resources with source line anchors and `links-to` relations back to the source note.
- Index local HTML pages with searchable extracted content, canonical URL aliases, and heading fragment anchors.
- Index MHTML/MHT web archives with searchable HTML content, canonical URL aliases, and heading fragment anchors.
- Index local HTML outbound links as derived URL resources with source line anchors and `links-to` relations back to the source page.
- Index MHTML/MHT outbound links as derived URL resources with `links-to` relations back to the source archive.
- Index browser bookmark export HTML as individual URL resources with searchable host aliases, fragment anchors, source line anchors, and `links-to` relations back to the export file.
- Index Chromium/Edge-style Bookmarks JSON as individual URL resources with searchable host/folder aliases, fragment anchors, source line anchors, and `links-to` relations back to the Bookmarks file.
- Index XBEL bookmark XML as individual URL resources with searchable host/folder aliases, fragment anchors, source line anchors, and `links-to` relations back to the XBEL file.
- Index Chromium/Edge-style browser History SQLite files as individual URL resources with visit-count aliases, fragment anchors, and `links-to` relations back to the history database.
- Index Firefox `places.sqlite` files as individual URL resources with bookmark titles, bookmark/folder aliases, visit-count aliases, fragment anchors, and `links-to` relations back to the places database.
- Index OPML subscription/link lists as individual URL resources with searchable host/feed/folder aliases, fragment anchors, source line anchors, and `links-to` relations back to the OPML file.
- Index RSS and Atom feed XML entries as individual URL resources with searchable feed aliases, category tags, fragment anchors, source line anchors, and `links-to` relations back to the feed file.
- Index sitemap XML URLs as individual URL resources with searchable sitemap tags, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Index `robots.txt` Sitemap directives as individual URL resources with searchable robots/sitemap tags, source line anchors, and `links-to` relations back to the source file.
- Index HAR/http archive entries as individual URL resources with page-title aliases, HTTP method/status tags, fragment anchors, source line anchors, and `links-to` relations back to the capture file.
- Index WARC response records as individual URL resources with extracted HTML content, fragment anchors, source line anchors, and `links-to` relations back to the WARC file.
- Index iCalendar event files with event/time/location anchors plus event URL resources, fragment anchors, and `links-to` relations back to the source calendar.
- Index email `.eml` message files with subject/from/to/date line anchors, searchable message content, body URL resources, fragment anchors, and `links-to` relations back to the source message.
- Index URLs found in small content-sniffed text files as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Index CSV/TSV URL columns as individual URL resources with host aliases, table aliases, category/tag aliases, fragment anchors, source row anchors, and `links-to` relations back to the source table.
- Index JSON/JSONL URL strings as individual URL resources with path aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Index YAML/TOML/INI/config URL strings as individual URL resources with fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Optionally fetch remote HTML for indexed web shortcuts and reuse the same title, content, canonical URL, and heading-anchor extraction.
- Index small content-sniffed text, config, and tabular files as searchable File content with TODO/FIXME/NOTE line anchors, text-structure key/section/path anchors, JSON/TOML/requirements-style dependency line beacons, and CSV/TSV column anchors.

Remaining:

- Wire the reusable panel into the actual ZeroSlack dock/global-control host.
- Add fuller PDF content extraction for remaining unsupported filters, complex encodings, and OCR, plus broader web source support beyond local HTML/MHTML/WARC, shortcuts, bookmarks/XBEL/history/places with bookmark metadata, OPML, feeds, sitemaps/robots.txt hints, HAR/http archives, iCalendar/email files, content-sniffed text URLs, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.
- Improve generic text beacon extraction and special-file readers without pursuing richer language parsing.

### Special-File Reader Roadmap

Reader boundary:

- Special readers exist only for formats that need format-aware extraction.
- Reader output is still content, aliases, tags, relations, diagnostics, and anchors with positions.
- Readers must not become document understanding, language semantics, or knowledge-modeling engines.

Phases:

1. Existing reader consolidation: keep PDF, browser SQLite, HTML/MHTML, HAR/WARC, OPML/RSS, sitemap/robots, iCalendar, and email described and tested as beacon/location readers; keep JSON/YAML/TOML/INI and CSV/TSV coverage described as text beacon rules, not special readers.
2. Office baseline readers: add Word/docx paragraphs/headings/tables/comments/links, Excel/xlsx sheets/cells/headers/formulas/error values/named ranges, and PowerPoint slide/title/body/notes/link beacons.
3. Engineering and design special readers: generic SQLite table/column/URL/sample-value beacons are in place; add Visio/vsdx page/shape/text/link beacons next. Compressed packages remain ordinary path-only file resources and are not a reader target.
4. Scanned/OCR readers: add OCR text, page/region anchors, confidence diagnostics, and fallback page-level jumps for scanned PDFs and image-heavy documents.
5. Reader contract and quality layer: standardize reader input limits, output schema, position types, diagnostics, partial extraction, unsupported/encrypted/too-large states, and timeout behavior.
6. Experience and performance: add reader toggles, incremental indexing, cancellation, failure UI, jump fallback behavior, deduplication, and ranking/noise tuning.

Current boundary enforcement:

- A shared path-only package-container guard keeps compressed packages and Office/Visio package containers out of expansion, generic text scanning, and derived-resource indexing. Compressed packages remain permanent path-only file targets; Office/Visio package containers may be upgraded only by document readers that emit real document positions. These resources are tagged `path-only` and `package-container`.
