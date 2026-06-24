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

Planned scope:

- File line anchors.
- Markdown heading/block anchors.
- PDF page/region anchors.
- Manual anchors and aliases.

## MVP 7: ZeroSlack Embedding

Goal: embed Pinloom UI into ZeroSlack without merging ownership boundaries.

Planned scope:

- Provide widget/dock/global-control friendly API.
- Keep `pinloom_core` independent.
- Reuse `pinloom_widgets` from both hosts.
