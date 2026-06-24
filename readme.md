# Pinloom

Pinloom is a standalone Qt application for locating personal and engineering materials: files, folders, PDFs, code snippets, web pages, notes, tags, aliases, anchors, and jump relationships.

It is not an Obsidian add-on and is not a ZeroSlack-private feature. Obsidian vaults are one supported library source. ZeroSlack embedding is a later UI host target.

## Current MVP: Directory Source Indexing

Implemented:

- Qt/CMake project skeleton.
- `pinloom_core` static library with resource, anchor, search, repository interfaces, an in-memory repository, and a SQLite/FTS5 schema draft.
- SQLite-backed `SqliteLibraryRepository` with versioned schema initialization.
- Persistent resources, tags, aliases, anchors, and FTS5 metadata search.
- `LibrarySource` and `IndexingService` abstractions.
- `DirectoryLibrarySource` for explicit normal-directory indexing.
- `pinloom_widgets` static library with a reusable `PinloomPanel` widget.
- `pinloom_app` standalone Qt application entry point.
- `pinloom_core_smoke_test` validating basic alias/tag search and FTS5 schema exposure.
- `pinloom_sqlite_repository_test` validating SQLite initialization, persistence, search, and idempotent upsert behavior.
- `pinloom_directory_source_test` validating explicit-root directory indexing and idempotent repository upserts.

Not implemented yet:

- Persistent library root management.
- Obsidian-specific markdown parsing for aliases, tags, wikilinks, and block ids.
- PDF content/region extraction, source-code symbol parsing, and web page indexing.
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
