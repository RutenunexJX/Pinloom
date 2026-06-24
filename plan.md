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

Planned scope:

- Add a SQLite repository implementation behind `ILibraryRepository`.
- Apply versioned schema migrations.
- Persist resources, tags, aliases, and anchors.
- Add tests for CRUD and simple search.

## MVP 3: Source Indexers

Goal: support library sources without making any one source special.

Planned scope:

- Define `LibrarySource` interfaces.
- Add normal folder/file indexing.
- Add Obsidian-friendly markdown indexing for tags, aliases, headings, wikilinks, and block ids.
- Keep Obsidian as one source adapter, not a core assumption.

## MVP 4: Precise Jump Targets

Goal: make anchors useful for real navigation.

Planned scope:

- File line anchors.
- Markdown heading/block anchors.
- PDF page/region anchors.
- Manual anchors and aliases.

## MVP 5: ZeroSlack Embedding

Goal: embed Pinloom UI into ZeroSlack without merging ownership boundaries.

Planned scope:

- Provide widget/dock/global-control friendly API.
- Keep `pinloom_core` independent.
- Reuse `pinloom_widgets` from both hosts.
