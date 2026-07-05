# Pinloom

Pinloom is being reset as a Listary-style deterministic anchor launcher.

The v1 product is not a general file content indexer, not Everything, not
Obsidian, not a PDF reader, and not a knowledge-base manager. Pinloom's job is
to let the user create named anchors in native applications, then return to
those exact places through a lightweight search overlay.

## V1 Loop

1. The user navigates to a target in a native app.
2. Pinloom captures the current position as a deterministic anchor.
3. The user names the anchor and can add tags or aliases.
4. Pinloom stores the native target and structured locator.
5. The user opens a compact search overlay later.
6. The user searches by anchor name, alias, or `#tag`.
7. Enter dispatches to the native app and jumps back to the exact position.

The preferred mental model is:

```text
Anchor = name + aliases + tags + target_app + target_file + locator
```

`Resource` or `File` is only the target container for an anchor. It is not the
primary product object.

## Product Boundary

Pinloom v1 focuses on deterministic, user-authored anchors. It should be
fast, compact, keyboard-first, and predictable.

Mainline:

- Manual anchor creation in native applications.
- Anchor naming, aliases, tags, pinned state, and recent-use recall.
- A Listary-style overlay with one search box and a compact result list.
- Search over anchor name, alias, tag, and target metadata.
- Dispatch by `target_app` and `locator_type` to native application jump
  executors.

Non-mainline for v1:

- Whole-disk search.
- General file-content indexing as the primary experience.
- Screenshot search, OCR search, semantic search, or automatic scanned-document
  understanding.
- Built-in PDF reading.
- Heavy three-pane library management UI.
- Expanding web, feed, browser-history, bookmark, archive, or generic reader
  surfaces as product headline features.

Existing indexing and reader work may remain in the repository as compatibility
or infrastructure, but it is frozen as a v1 product direction. New work should
not widen those surfaces unless it directly supports the anchor launcher loop.

## Priority Executors

1. PDF-XChange Editor
   - Unified PDF host for v1.
   - Supports page, zoom, viewrect, highlight, and `usept`.
   - Text PDFs and scanned PDFs are treated the same: the user names an anchor
     against a selected PDF context and Pinloom stores page plus rectangle.
   - The current default capture path is a selected-PDF fallback: when native
     PDF-XChange current-view capture is not available, Pinloom uses the
     selected PDF file with page 1 and an approximate full-page rectangle.
     Raw coordinate entry is an advanced/debug fallback, not the main user
     flow.
   - Example locator:

```json
{"type":"pdfxchange.rect","page":12,"rect":[420,860,780,920],"zoom":250,"unit":"pt"}
```

2. Excel
   - Target is file plus sheet plus range or named range.
   - Jump executor opens Excel and selects the target range.

3. Word
   - Target is file plus bookmark.
   - Bookmarks are preferred over page numbers or fragile text fragments.

4. PowerPoint
   - Target is file plus slide plus shape id or shape name.
   - Jump executor opens the deck, goes to the slide, and selects the shape
     when possible.

5. Visio
   - Target is file plus page plus shape UniqueID.
   - Engineering drawings are high priority, just behind PDF and Excel.

## Current Implementation Inventory

The current codebase already has useful foundations:

- C++17 / Qt6 / CMake project split into `pinloom_core`, `pinloom_widgets`, and
  `pinloom_app`.
- SQLite persistence with resources, aliases, tags, anchors, anchor locator
  fields, anchor FTS, ranking, pinned state, recent-use signals, and library
  roots.
- In-memory and SQLite repositories used by tests.
- Search ranking that already considers anchors, aliases, tags, pinned items,
  recent use, and contextual signals.
- A reusable Qt panel that can be embedded and can expose selected targets to a
  host.
- The default Qt panel now behaves as a lightweight anchor launcher surface:
  search-first, compact result list, anchor locator summaries, keyboard
  activation, alias/tag editing, PDF capture from selected context, edit entry,
  and delete placeholder.
- PDF-XChange manual jump execution now has a tested command builder for page
  and rectangle locators, launcher activation integration, and executable path
  resolution through `PINLOOM_PDFXCHANGE_PATH`, common install paths, or host
  injection. The local validation flow and environment-blocked result are
  documented in `docs/pdfxchange_validation.md`.

The mismatch is intentional technical debt for the reset:

- `Resource` is still the primary persisted object.
- `Anchor` now carries the v1 locator fields, but it is still exposed through
  the resource-attached compatibility API while the UI converges.
- Search still indexes resource content and broad source metadata.
- Folder management and resource-library controls still exist for
  compatibility, but they are no longer the default first surface.
- Native PDF-XChange current-view/selection capture is still pending; the
  current PDF UX uses selected-PDF fallback capture and keeps raw coordinates
  in an advanced path.
- Pinloom Clip is now wired into `pinloom_app.exe` as a resident text
  clipboard MVP with SQLite persistence, tray menu, `Ctrl+Shift+V` picker,
  temporary history insertion, and explicit Save Clip metadata.

The next implementation phases should converge these foundations toward the v1
anchor model instead of expanding source indexing.

## Build

The first verified local toolchain is Qt 6.10.2 with MinGW and CMake/Ninja from
`E:\QT6`.

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

Pinloom Clip starts with the app. Copy text, press `Ctrl+Shift+V` or use the
tray menu's `Show Clipboard`, search the picker, and press Enter to paste the
selected text clip into the current application. `Save Clip` turns a temporary
clip into a named Saved Clip with aliases, tags, and pinned state.

## Phase 0 Validation

Phase 0 resets the product specification only. It does not change the C++ data
model, SQLite schema, or Qt UI implementation yet.

Validation target:

- Documentation states the deterministic anchor launcher direction.
- Existing build and tests still pass.
- Changes are committed and pushed to `origin/main`.
