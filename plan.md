# Pinloom Plan

## MVP 1: Project Bootstrap

Goal: establish a standalone Qt/CMake application skeleton with reusable layers and a first commit.

Scope:

- Create CMake project and targets: `pinloom_core`, `pinloom_widgets`, `pinloom_app`.
- Keep `pinloom_core` free of ZeroSlack UI dependencies.
- Define minimal resource, tag, alias, anchor, repository, and search types.
- Define SQLite/FTS5 schema draft without building full persistence yet.
- Add a reusable widget suitable for both standalone app and later ZeroSlack embedding.
- Add a smoke/core test.
- Build, test, update docs, commit, and push.

Status:

- Project skeleton: done
- Core target: done
- Widgets target: done
- App target: done
- SQLite/FTS5 strategy draft: done
- Smoke/core test: done
- Build/test verification: done
- Commit: done
- Push: origin/main configured for project commit

## MVP 2: Local Library Persistence

Goal: introduce a SQLite-backed repository while keeping the in-memory repository useful for tests.

Scope:

- Add a SQLite repository implementation behind `ILibraryRepository`.
- Apply versioned schema migrations.
- Persist resources, tags, aliases, and anchors.
- Add tests for CRUD and simple search.

Status:

- SQLite repository: done
- Versioned schema initialization: done
- Resource/tag/alias/anchor persistence: done
- FTS5 metadata search: done
- Tests: done
- Build/test verification: done
- Commit/push: release target is `origin/main`

## MVP 3: Source Indexers

Goal: support library sources without making any one source special.

Scope:

- Define `LibrarySource` interfaces.
- Add normal folder/file indexing.
- Add Obsidian-friendly markdown indexing for tags, aliases, headings, wikilinks, and block ids.
- Keep Obsidian as one source adapter, not a core assumption.

Status:

- `LibrarySource` interface: done
- `IndexingService`: done
- Explicit normal directory source: done
- Markdown/Obsidian-specific parsing: deferred
- Tests: done
- Build/test verification: done
- Commit/push: release target is `origin/main`

## MVP 4: Usable Locator UI

Goal: make the standalone app use the real repository and directory source through a reusable widget.

Scope:

- Inject repository into `PinloomPanel`.
- Use SQLite repository from the standalone app.
- Add folder selection, refresh index, search results, and basic open behavior.
- Keep widget reusable for future ZeroSlack embedding.

Status:

- Repository-injected widget: done
- Default standalone app SQLite database: done
- Add folder and refresh index controls: done
- Search and open selected resource: done
- Tests: done
- Build/test verification: done
- Commit/push: release target is `origin/main`

## MVP 5: Library Root Management

Goal: make saved library folders persistent and manageable through the reusable UI.

Scope:

- Add `LibraryRoot` to the core model.
- Persist roots in SQLite schema version 2.
- Add root CRUD to repository interfaces and implementations.
- Add indexing service support for saved enabled roots and rebuilds.
- Add UI controls for add, remove, refresh selected, refresh all, and rebuild all.

Status:

- Core `LibraryRoot` model: done
- SQLite schema v2 and root CRUD: done
- Saved-root indexing and rebuild support: done
- Root management UI: done
- Tests: done
- Build/test verification: done
- Commit/push: release target is `origin/main`

## MVP 6: Precise Jump Targets

Goal: make anchors useful for real navigation.

Scope:

- File line anchors.
- Markdown heading/block anchors.
- PDF page/region anchors.
- Manual anchors and aliases.

Status:

- Anchor-aware `SearchResult`: done
- SQLite schema v3 anchor search index: done
- Markdown heading/block extraction: done
- Built-in text preview for line anchors: done
- PDF page/region anchors: deferred
- Manual anchor UI: deferred
- Tests: done
- Build/test verification: done
- Commit/push: release target is `origin/main`

## MVP 7: Search Results Cleanup

Goal: make search results easier to scan and reduce root-path noise.

Scope:

- Replace internal `fts` display text with readable result labels.
- Prioritize anchor/title/filename/alias/tag matches ahead of full-path matches.
- Keep full paths available through result text and tooltips.
- Keep the existing `QListWidget`-based result list.

Status:

- Match semantics cleanup: done
- Search result formatting cleanup: done
- Root-path noise demotion: done
- Tests: done
- Build/test verification: done
- Commit/push: release target is `origin/main`

## MVP 8: ZeroSlack Embedding

Goal: embed Pinloom UI into ZeroSlack without merging ownership boundaries.

Scope:

- Provide widget/dock/global-control friendly API.
- Keep `pinloom_core` independent.
- Reuse `pinloom_widgets` from both hosts.
- Let hosts seed search text and focus the locator.
- Let hosts hide standalone management/editing chrome for compact dock/global-control embedding.
- Let hosts apply active search/filter/ranking context as one snapshot when project or document context changes.
- Let hosts inspect result count and navigate result selection through API for command palette flows.
- Let hosts observe result-count changes after search/filter refreshes.
- Let hosts activate the current result through API for command palette or global-shortcut flows.
- Let hosts intercept selected result activation while preserving standalone fallback open behavior.
- Document ownership boundaries between Pinloom and an embedding host.

Status:

- Repository-injected widget: done in earlier MVPs.
- Host-facing panel options and search control API: done.
- Host-selectable embedded chrome options: done.
- Host current-open-target query and change notification APIs for previews/status surfaces: done.
- Host current result target list snapshot API: done.
- Host resource-id-based open-target query API: done.
- Host resource-id-based activation API: done.
- Host current-related-target query API for previews/status surfaces: done.
- Host resource-id-based related-target query API: done.
- Host-triggered result count and selection navigation APIs: done.
- Host direct result selection by row or resource id: done.
- Host result-count change notifications: done.
- Host current result snapshot change notifications: done.
- Host status text query and change notifications: done.
- Host library-root snapshot query, direct selection, and change notifications: done.
- Host-triggered library-root add/remove API: done.
- Host-triggered library-root pin/unpin API: done.
- Host-triggered library-root enable/disable API: done.
- Host-triggered current-result activation API: done.
- Host-controllable optional remote HTML fetching toggle: done.
- Host-triggered selected-root, root-by-id, all-root, and rebuild indexing APIs with structured results: done.
- Host indexing completion callback and latest-result query API: done.
- Host resource-id-based manual alias and anchor editing API: done.
- Host resource-id-based resource pin/unpin API: done.
- Host resource-id-based relation create/update/remove API: done.
- Host-required resource-kind, tag, and location-prefix filtering APIs: done.
- Host relation-aware context-resource ranking API: done.
- Host relation-label-scoped context ranking API: done.
- Atomic host search/filter/ranking context snapshot API: done.
- Result row included in host open-target payloads: done.
- Rich host activation payload with resource metadata, match/context details, score, and anchors: done.
- Standalone fallback behavior: done.
- Embedding ownership boundary documentation: done.
- Tests: done.
- Build/test verification: done.

## MVP 9: Obsidian-Friendly Indexing

Goal: make Obsidian vaults useful as library sources without making Pinloom an Obsidian add-on.

Scope:

- Parse YAML frontmatter aliases and tags.
- Parse inline Markdown tags.
- Parse wikilinks and block references.
- Keep regular Markdown folders working the same way.
- Add tests for plain Markdown, Obsidian-flavored Markdown, and mixed folders.

Status:

- Markdown heading and block id extraction: done in MVP 6.
- Frontmatter aliases/tags: done.
- Inline tags, wikilinks, and block references: done.
- Markdown body content extraction/search: done.
- Markdown task checkbox line anchors/search: done.
- Local relative Markdown link aliases/line anchors and indexed `links-to` relations: done.
- Obsidian wikilink aliases/line anchors and indexed `links-to` relations: done.
- Tests: done.
- Build/test verification: done.

## MVP 10: PDF Navigation

Goal: make PDF resources landable beyond file-level open behavior.

Scope:

- Index PDF metadata and page anchors.
- Store page-level anchors in the existing anchor model.
- Open PDF hits at a page when the platform supports it.
- Leave region anchors for a later focused pass.

Status:

- PDF files recognized as resources: done in MVP 3.
- PDF page anchors: done.
- PDF outline/bookmark direct, named, and indirect named destination anchors: done.
- PDF metadata extraction: done.
- Page-open behavior: done for viewers that honor `file.pdf#page=N`.
- PDF annotation region anchors: done.
- PDF URI link annotation URL resources and `links-to` relations: done.

## MVP 11: Code-Aware Locator

Goal: make engineering source trees searchable by symbols and jump targets, not only file names.

Scope:

- Treat source-code files as first-class resources.
- Extract lightweight line and symbol anchors for common engineering languages.
- Rank exact symbol and filename matches ahead of path matches.
- Keep source parsing modular so language-specific logic does not leak into repository storage.

Status:

- Generic file indexing: done in MVP 3.
- Code resource classification: done.
- Lightweight symbol anchors for common engineering languages: done.
- Rust, Go, Java, and C# lightweight symbol anchors: done.
- Test case/suite anchors for C++ GoogleTest and JS/TS tests: done.
- CMake project/target/package/option/function/macro/test anchors: done.
- Makefile target anchors and Dockerfile stage/base/copy anchors: done.
- GitHub Actions workflow/job/step/action/run anchors: done.
- GitLab CI stage/job/needs/script anchors: done.
- compile_commands.json line anchors and `compiles` relations: done.
- Dependency/import line anchors for common engineering languages: done.
- Go import block and JS/TS dynamic import anchors: done.
- Shell, PowerShell, batch, and shebang script symbol/dependency anchors: done.
- TODO/FIXME/NOTE comment line anchors for source files: done.
- Exact symbol and filename ranking baseline: done through anchor-first and filename-before-path ranking.
- Rich language-specific parsers: deferred.

## MVP 12: Manual Anchors And Relationships

Goal: let users curate the library map directly when automatic indexing is not enough.

Scope:

- Add UI for manual aliases and anchors.
- Persist related-resource links.
- Show relationships in a compact result detail surface.
- Keep the main locator fast and list-oriented.

Status:

- Manual anchor resource kind exists in the model: done.
- Related-resource persistence: done.
- Indexed `links-to` relations from local Markdown links: done.
- Compact relationship summary in result details: done.
- Host-facing current related-target API: done.
- Host-facing resource-id related-target API: done.
- Host-facing relation create/update/remove API: done.
- Manual anchor and alias editing UI: done.
- Resource-id-based host APIs for manual alias and anchor editing: done.

## MVP 13: Ranking And Recall

Goal: make Pinloom remember which jumps matter in the user's actual workflow.

Scope:

- Track recent opens and frequently used jumps.
- Support pinned resources or pinned roots.
- Add project/context weighting for embedded use.
- Keep ranking explainable through match labels and details.

Status:

- Match-type ranking baseline: done in MVP 7.
- Resource-level recency/frequency signals: done.
- Pinned resources: done.
- Resource-id-based host resource pinning API: done.
- Context-aware ranking: done.
- Relation-aware context-resource ranking: done.
- Relation-label-scoped context ranking: done.
- Matched relation-note payloads for context-ranked results: done.
- Host-required resource-kind, tag, and location-prefix filtering: done.
- Anchor-level usage history and ranking: done.
- Pinned roots and root-level ranking boosts: done.
- Exact-match ranking within each match type: done.
- Result tooltip details for match fields, anchors, host context matches, and matched relation notes: done.

## MVP 14: Integration And Source Refinement

Goal: close the gap between the reusable Pinloom layer and the real host/source surface.

Scope:

- Wire Pinloom into the actual ZeroSlack dock/global-control host.
- Extend source coverage beyond normal files, Markdown, PDFs, and code.
- Add more precise jumps where the current model already has anchor types.
- Keep standalone behavior working while embedded behavior gains host context.

Status:

- Local web shortcut indexing as URL resources, including desktop link shortcuts: done.
- URL fragment anchors: done.
- URL open behavior through host interception or standalone fallback: done.
- URL fragment anchor fallback opening for web/local HTML targets: done.
- PDF annotation region anchors: done.
- PDF URI link annotation URL resources and `links-to` relations: done.
- PDF outline/bookmark direct, named, and indirect named destination page anchors: done.
- Lightweight PDF text extraction/search for uncompressed text streams, including `TJ` text arrays with split glyph runs: done.
- Lightweight PDF text extraction/search for FlateDecode text streams: done.
- Lightweight PDF text extraction/search for ASCIIHexDecode streams and literal octal escapes: done.
- Lightweight PDF text extraction/search for ASCII85Decode streams: done.
- Lightweight PDF text extraction/search for RunLengthDecode streams: done.
- Lightweight PDF text extraction/search for LZWDecode streams: done.
- Ordered PDF stream filter chains for supported filters: done.
- UTF-16 BOM PDF title/content string decoding: done.
- Basic PDF ToUnicode CMap decoding for font-encoded content streams: done.
- Markdown body content extraction/search: done.
- Markdown task checkbox line anchors/search: done.
- Local relative Markdown link aliases/line anchors and indexed `links-to` relations: done.
- Markdown external link URL resources with source line anchors and `links-to` relations: done.
- Markdown reference-style external link URL resources with source line anchors and `links-to` relations: done.
- Local HTML page content extraction/search, canonical URL aliases, and heading fragment anchors: done.
- MHTML/MHT web archive content extraction/search, canonical URL aliases, and heading fragment anchors: done.
- Local HTML and MHTML/MHT outbound link URL resources with `links-to` relations: done.
- Browser bookmark export HTML and Chromium/Edge-style Bookmarks JSON indexing as individual URL resources: done.
- Chromium/Edge-style browser History SQLite indexing as individual URL resources: done.
- Firefox places.sqlite indexing as individual URL resources with bookmark metadata: done.
- OPML subscription/link list indexing as individual URL resources: done.
- RSS and Atom feed XML entry indexing as individual URL resources: done.
- Sitemap XML URL indexing as individual URL resources: done.
- robots.txt Sitemap directive indexing as individual URL resources with source line anchors and `links-to` relations: done.
- HAR/http archive entry indexing as individual URL resources: done.
- WARC response record indexing as individual URL resources with extracted HTML content, source line anchors, and `links-to` relations: done.
- iCalendar event URL indexing with event/time/location anchors and `links-to` relations: done.
- Plain-text URL list indexing as individual URL resources with source line anchors and `links-to` relations: done.
- JSON/JSONL URL string indexing as individual URL resources with source line anchors and `links-to` relations: done.
- YAML/TOML/INI/config URL string indexing as individual URL resources with source line anchors and `links-to` relations: done.
- CSV/TSV URL-column indexing as individual URL resources with source row anchors and `links-to` relations: done.
- Optional remote HTML fetching/content extraction for indexed web shortcuts: done.
- Small plain-text/log/config/manifest/tabular file content indexing with TODO/FIXME/NOTE, config key/section/path, package dependency, and CSV/TSV column line anchors: done.
- Actual ZeroSlack host integration: pending.
- Fuller PDF content extraction for remaining unsupported filters, complex encodings, and OCR: pending.
- Broader web source support beyond local HTML/MHTML/WARC, shortcuts, bookmarks/history/places with bookmark metadata, OPML, feeds, sitemaps/robots.txt hints, HAR/http archives, iCalendar files, text URL lists, JSON/JSONL, YAML/TOML/INI/config files, and CSV/TSV URL columns: deferred.
- Richer source-code parsing: deferred until lightweight symbols prove insufficient.
