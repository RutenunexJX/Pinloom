# Pinloom Goal

## Long-Term Goal

Pinloom is a fast locator for personal and engineering knowledge. It helps users jump to the right material, anchor, note, file, PDF page, code line, folder, web page, alias, tag, or related resource without becoming a full-disk search tool.

Pinloom's long-term product shape is a personal knowledge locator and jump layer. It should answer "where do I need to go next?" better than "which files contain this string?" Search quality is measured by how quickly a user lands on the right source, anchor, page, code line, or related note inside a curated library.

## Architecture Principles

- Standalone first: Pinloom must run as a complete Qt application.
- Embeddable UI: reusable widgets must later fit into ZeroSlack as a dock/global-control panel.
- Core independence: `pinloom_core` must not depend on ZeroSlack UI or app-specific host behavior.
- Source neutrality: Obsidian vaults, normal folders, PDFs, code snippets, notes, and manual anchors are all library sources.
- Obsidian friendliness: markdown tags, aliases, headings, wikilinks, and block ids should be indexed without making Pinloom an Obsidian add-on.
- Precise anchors: search results should be able to land on PDF pages/regions, file lines, markdown headings/blocks, URLs, and manual targets.
- Locator search: optimize for curated library positioning and fast jumps, not whole-disk crawling.

## Long-Term Roadmap

- ZeroSlack embedding boundary: make `pinloom_widgets` usable by standalone Pinloom and ZeroSlack without transferring ownership of core state to either host.
- Obsidian-friendly indexing: parse frontmatter aliases, inline tags, wikilinks, and block references while keeping Markdown support useful outside Obsidian.
- PDF navigation: index PDF metadata and pages first, then add page and region jump targets.
- Code-aware locator: index source files by paths, symbols, line anchors, and project-relevant tags.
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
- Let embedding hosts inspect related targets for the current selection without parsing UI text.
- Let embedding hosts inspect result count and navigate result selection through API for command palette flows.
- Let embedding hosts observe result-count changes after search/filter refreshes.
- Let embedding hosts activate the current result through API for command palette or global-shortcut flows.
- Let embedding hosts control the optional remote HTML fetch setting used during indexing.
- Let embedding hosts trigger selected-root, all-root, and rebuild indexing flows with structured results.
- Let embedding hosts hard-filter searches by required resource kinds, tags, and location prefixes while using context tags, prefixes, and related resource ids for ranking.
- Let embedding hosts apply active search/filter/ranking context as one snapshot for project/document changes.
- Include resource kind, title, matched field, matched context signals, score, and anchor details in host activation payloads.
- Include relation label, note, direction, and related resource target details in host related-target payloads.
- Keep standalone behavior as the default fallback.
- Document which responsibilities belong to Pinloom and which belong to the embedding host.

Remaining follow-up:

- Wire the boundary into the actual ZeroSlack dock/global-control host.

### Obsidian-Friendly Indexing MVP

Completed:

- Parse YAML frontmatter aliases and tags.
- Parse inline `#tags`, `[[wikilinks]]`, and block references.
- Extract searchable Markdown body content while excluding frontmatter metadata from body text.
- Extract Markdown task checkbox lines as searchable file-line anchors.
- Extract local relative Markdown links as searchable aliases, file-line anchors, and indexed `links-to` relations.
- Extract Obsidian wikilinks as searchable file-line anchors and indexed `links-to` relations.
- Preserve normal Markdown behavior for non-Obsidian folders.
- Add focused tests around mixed plain-Markdown and Obsidian vault inputs.

### PDF Navigation MVP

Completed:

- Store PDF page anchors.
- Add basic PDF metadata extraction.
- Open search hits at the intended page when the platform viewer supports it.
- Extract PDF annotation rectangles as region anchors.

### Code-Aware Locator MVP

Completed:

- Index source-code files as first-class resources.
- Add line and symbol anchors for common languages used in engineering work.
- Add lightweight Rust, Go, Java, and C# symbol anchors.
- Add lightweight test case/suite anchors for C++ GoogleTest and JS/TS tests.
- Add dependency/import line anchors for common engineering languages.
- Support Go import block and JS/TS dynamic import dependency anchors.
- Extract TODO/FIXME/NOTE comment line anchors for engineering source files.
- Rank exact symbol and filename matches ahead of broad path matches.

Remaining follow-up:

- Replace lightweight regular-expression parsing with richer language-specific parsers if precision becomes a bottleneck.

### Manual Anchors And Relationships MVP

Completed:

- Persist related-resource links.
- Persist indexed `links-to` relations discovered from local Markdown links.
- Surface relationships in compact result details without turning the main result list into a graph browser.
- Expose current related targets through the host-facing panel API.
- Add UI affordances for creating manual aliases and anchors.

### Ranking And Recall MVP

Completed:

- Track recently opened resources and frequently used resource activations.
- Track recently opened anchors and frequently used precise jumps.
- Support pinned resources.
- Support pinned library roots as project-level recall signals.
- Support host-required kind/tag/location filters alongside host context ranking.
- Apply recall signals as small ranking boosts without letting weak path matches outrank stronger match types.
- Boost exact title, filename, alias, tag, content, path, and anchor matches within their match type.
- Add project/context weighting, including relation-aware active-resource weighting, while keeping ranking understandable.
- Show matched fields, anchors, and host context matches in result tooltips.

### Integration And Source Refinement MVP

In progress:

Completed:

- Index local web shortcut files as URL resources without requiring network access.
- Preserve URL fragments as searchable URL anchors.
- Open URL resources through host interception or the standalone fallback URL opener.
- Preserve matched URL fragment anchors when standalone fallback opens web pages or local HTML pages.
- Extract PDF annotation rectangles as searchable region anchors.
- Extract searchable PDF text from uncompressed text content streams.
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
- Index browser bookmark export HTML and Chromium/Edge-style Bookmarks JSON as individual URL resources with searchable host/folder aliases and fragment anchors.
- Index Chromium/Edge-style browser History SQLite files as individual URL resources with visit-count aliases, fragment anchors, and `links-to` relations back to the history database.
- Index Firefox `places.sqlite` files as individual URL resources with bookmark titles, bookmark/folder aliases, visit-count aliases, fragment anchors, and `links-to` relations back to the places database.
- Index OPML subscription/link lists as individual URL resources with searchable host/feed/folder aliases and fragment anchors.
- Index RSS and Atom feed XML entries as individual URL resources with searchable feed aliases, category tags, and fragment anchors.
- Index sitemap XML URLs as individual URL resources with searchable sitemap tags and fragment anchors.
- Index HAR/http archive entries as individual URL resources with page-title aliases, HTTP method/status tags, fragment anchors, source line anchors, and `links-to` relations back to the capture file.
- Index plain-text URL list files as individual URL resources with host aliases, fragment anchors, source line anchors, and `links-to` relations back to the source list.
- Index CSV/TSV URL columns as individual URL resources with host aliases, table aliases, category/tag aliases, fragment anchors, source row anchors, and `links-to` relations back to the source table.
- Index JSON/JSONL URL strings as individual URL resources with path aliases, fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Index YAML/TOML/INI/config URL strings as individual URL resources with fragment anchors, source line anchors, and `links-to` relations back to the source file.
- Optionally fetch remote HTML for indexed web shortcuts and reuse the same title, content, canonical URL, and heading-anchor extraction.
- Index small plain-text, log, config, manifest, and tabular files as searchable File content with TODO/FIXME/NOTE line anchors, config key/section/path anchors, package dependency anchors, and CSV/TSV column anchors.

Remaining:

- Wire the reusable panel into the actual ZeroSlack dock/global-control host.
- Add fuller PDF content extraction for remaining unsupported filters, complex encodings, and OCR, plus broader web source support beyond local HTML, shortcuts, bookmarks/history/places with bookmark metadata, OPML, feeds, sitemaps, HAR/http archives, text URL lists, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns.
- Add richer source-code parsing if lightweight symbol extraction becomes too noisy.
