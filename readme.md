# Pinloom

Pinloom is a standalone Qt application for locating personal and engineering materials: files, folders, PDFs, code snippets, web pages, notes, tags, aliases, anchors, and jump relationships.

It is not an Obsidian add-on and is not a ZeroSlack-private feature. Obsidian vaults are one supported library source. ZeroSlack embedding is a later UI host target.

## Current MVP: Search Results Cleanup

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
- Built-in read-only text preview for line-based anchors.
- Cleaned-up search result display with readable type labels and tooltips.
- Search ordering that prioritizes anchors, titles, filenames, aliases, and tags before full-path matches.
- `pinloom_widgets` static library with repository-injected reusable `PinloomPanel`.
- `pinloom_app` standalone Qt application entry point using a default SQLite database under `QStandardPaths::AppDataLocation`.
- Locator UI for adding/removing folders, refreshing selected/all folders, rebuilding the index, searching, and opening selected resources.
- `pinloom_core_smoke_test` validating basic alias/tag search and FTS5 schema exposure.
- `pinloom_sqlite_repository_test` validating SQLite initialization, persistence, search, and idempotent upsert behavior.
- `pinloom_directory_source_test` validating explicit-root directory indexing and idempotent repository upserts.
- `pinloom_widget_smoke_test` validating repository injection into the reusable widget.

Not implemented yet:

- Obsidian-specific markdown parsing for aliases, tags, and wikilinks.
- PDF content/region extraction, source-code symbol parsing, and web page indexing.
- PDF-page/region jumps and manual anchor editing UI.
- Production search ranking.
- ZeroSlack dock/global-control integration.

## Build

The first verified local toolchain is Qt 6.10.2 with MinGW and CMake/Ninja from `E:\QT6`.

```powershell
$env:PATH='E:\QT6\Tools\mingw1310_64\bin;' + $env:PATH
E:\QT6\Tools\CMake_64\bin\cmake.exe -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=E:\QT6\6.10.2\mingw_64 -DCMAKE_MAKE_PROGRAM=E:\QT6\Tools\Ninja\ninja.exe -DCMAKE_CXX_COMPILER=E:\QT6\Tools\mingw1310_64\bin\g++.exe
E:\QT6\Tools\CMake_64\bin\cmake.exe --build build
$env:PATH='E:\QT6\6.10.2\mingw_64\bin;E:\QT6\Tools\mingw1310_64\bin;' + $env:PATH
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
