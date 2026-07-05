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
- Pinloom Inbox capture for user-selected local file objects that should enter
  the name, alias, tag, search, and launch loop.
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
     against a foreground or selected PDF context and Pinloom stores page plus
     rectangle.
   - The current Command Window capture path is a foreground PDF-XChange
     fallback: when `Ctrl+Space` is pressed, Pinloom remembers the foreground
     PDF-XChange window, resolves the PDF file path when the window title/text
     exposes it, then `k n` opens a lightweight anchor naming dialog. This no
     longer requires searching the PDF name in Pinloom first.
   - Native current-view coordinates are still pending. Until they are stable,
     foreground capture pre-fills the PDF file and uses page 1 plus an
     approximate full-page rectangle; page/rect/zoom remain editable in the
     advanced locator fallback.
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
- The default Qt panel now behaves as a lightweight launcher bar: a focused
  horizontal search box with an optional compact result list. It searches
  anchors and Saved Clips, with management controls kept off the default
  surface.
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
- Foreground PDF-XChange file capture is now wired into the Command Window:
  focus an open PDF in PDF-XChange, press `Ctrl+Space`, type `k n`, name the
  anchor, and save. Pinloom uses the foreground PDF path when PDF-XChange
  exposes it, so the user does not need to search the PDF name first. Native
  current-page/current-rectangle capture is still pending; page 1 plus a
  full-page rectangle remains the explicit fallback locator.
- Pinloom Clip is now wired into `pinloom_app.exe` as a resident text
  clipboard MVP with SQLite persistence, tray menu, unified `Ctrl+Space`
  launcher access through ordinary Command Window search plus explicit `c s`
  search/insert and `c n` save commands, automatic system clipboard text
  capture, temporary history insertion, row timestamps, and explicit Save Clip
  metadata.
- Pinloom Inbox is implemented as a local file object capture MVP. It is
  Link-only by default: Pinloom records the original file path and does not
  move or copy user files. Dropping a file on the Command Window or using
  `i n` from a recent Explorer selection saves a searchable Inbox file with
  name, alias, tag, pinned, and default-app launch behavior. Re-saving the
  same path updates the existing Inbox entry instead of creating duplicates.
- The Command Window is the default global `Ctrl+Space` entry. It restores a
  compact command/search window and focuses one input; on Windows this can
  conflict with IMEs or another application that already owns `Ctrl+Space`.

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

Pinloom starts as a resident app with one global shortcut: `Ctrl+Space` summons
the Command Window and focuses one search/command input. Type an ordinary query
to search unified results across Anchors, Saved Clips, Inbox files, and regular
file/resource results. Result rows are labeled by type such as `[Anchor]`,
`[Clip]`, `[Inbox]`, and `[File]`; Enter jumps Anchors, inserts Saved Clips into
the current foreground app, and opens Inbox/File results with the default app.
With a unified result selected, press Right Arrow (`->`) to open its compact
action list. The action list supports Up/Down selection, Enter to run the
selected action, and Esc or Left Arrow to return to ordinary results. Actions
include the primary Jump/Insert/Open operation plus object-level metadata
actions: Rename, Edit aliases, Edit tags, Pin/Unpin, Delete / Remove, and
Restore. Delete / Remove is a confirmed Pinloom soft delete; it hides Pinloom's
record and does not delete the original file, native PDF, Inbox source file, or
external clipboard source content. Type `restore <query>` or `trash <query>` to
search soft-deleted Pinloom Entries, then press Right Arrow and choose Restore.
Actions that are not valid for the current object state remain visible with a
disabled reason instead of failing silently.

The explicit command namespaces remain available. Type `c` to see Clip commands.
Type `c s` to search all insertable Clip rows (temporary history plus Saved
Clips), or `c s <query>` to search by name, alias, tag, preview, or content;
Enter inserts the selected row into the foreground app. Type `c n` to choose a
recent temporary clipboard item and save it as a named Saved Clip with tags,
aliases, and pinned state. The tray `Show Clipboard` action routes back to this
same `c s` launcher path instead of opening a separate picker as the primary
workflow.

Type `k` to see anchor commands. The recommended PDF-XChange flow is: open or
focus the target PDF in PDF-XChange Editor, press `Ctrl+Space`, type `k n`,
enter the anchor name plus optional aliases/tags/pinned state, then save. The
dialog defaults `target_app` to PDF-XChange and `target_file` to the foreground
PDF path when PDF-XChange exposes it, so you do not need to search the PDF name
inside Pinloom before adding the anchor. Current page/rectangle capture is still
a fallback: page defaults to 1 and the rectangle defaults to an approximate
full-page region until precise PDF-XChange coordinate capture lands. The capture
dialog shows a readable locator summary with file, page, rectangle, zoom, and
capture source. Failure statuses distinguish "PDF-XChange not detected",
"document/file path not exposed", ambiguous indexed-PDF matches, and missing
view-state data.

Type `i` to see Inbox commands. Drop a local file on the Command Window, then
press Enter on `i n` to save it as a Link-mode Inbox file; if no file is
pending, `i n` tries the file selection from the Explorer window that was in
front before `Ctrl+Space` opened Pinloom. Type `i s <query>` to open the main
Pinloom search for archived Inbox files. Inbox is not a file manager and does
not parse file contents, sync files, or move/copy files in this MVP.

Clip rows show when each item was captured. The resident Clip command view
shows temporary history alongside Saved Clips; once a temporary item is saved,
it is no longer mixed into temporary history. Saved Clips remain searchable by
name, alias, and tag in ordinary Command Window queries, the main launcher bar,
and in `c` mode.
Re-copying exact text already stored as a Saved Clip is ignored by the content
hash duplicate check. The current Clip MVP captures and inserts text only;
rich content is future work. Clip insertion defaults to restoring the original
clipboard text after a successful paste; this can be changed in Pinloom
Settings. The same settings surface contains the app blacklist, sensitive-text
markers, maximum text size, temporary history limit, and history TTL so Clip
history is not an unbounded default store.

Internally, the Command Window now routes ordinary work through
`command -> entry/action -> result`: commands search `PinloomEntry` objects,
actions return `success`, `message`, `diagnostics`, and a `next UI hint`, and
the Qt UI is only one caller of that protocol. This keeps the path reusable for
future hosts such as ZeroSlack without adding a new framework.

## Phase 0 Validation

Phase 0 resets the product specification only. It does not change the C++ data
model, SQLite schema, or Qt UI implementation yet.

Validation target:

- Documentation states the deterministic anchor launcher direction.
- Existing build and tests still pass.
- Changes are committed and pushed to `origin/main`.
