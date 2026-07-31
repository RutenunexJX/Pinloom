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

Directory crawling, library-root management, and relationship-graph storage
have been removed from the runtime. New work should not reintroduce those
surfaces unless they directly support the anchor launcher loop.

## Priority Executors

1. SumatraPDF
   - Unified PDF host for v1.
   - Supports page, zoom, text search, and rectangle-derived scroll targets
     through SumatraPDF command-line arguments.
   - Text PDFs and scanned PDFs are treated the same: the user names an anchor
     against a foreground or selected PDF context and Pinloom stores page plus
     rectangle.
   - The Command Window capture path uses SumatraPDF 3.7 DDE. When
     `Shift+Space` is pressed, Pinloom remembers the foreground SumatraPDF
     window and obtains the active PDF full path, page, and zoom.
   - Anchor Library Preview uses the official SumatraPDF 3.7
     `sumatrapdf-tool.exe` beside `SumatraPDF.exe` to render a page directly.
     Rectangle anchors are cropped to their marked content with a small context
     margin; Preview does not open or screenshot the reader window.
   - `k n` hides the Command Window and opens a transparent capture layer over
     SumatraPDF. Drag a rectangle inside one PDF page; right-click or press
     `Esc` to cancel. Pinloom stores the two DDE page coordinates directly.
   - Example locator:

```json
{"type":"sumatrapdf.rect","page":12,"rect":[420,860,780,920],"zoom":250,"unit":"pt"}
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
- SQLite persistence with resources, aliases, tags, canonical anchor locators,
  anchor FTS, ranking, pinned state, and recent-use signals.
- In-memory and SQLite repositories used by tests.
- Search ranking that already considers anchors, aliases, tags, pinned items,
  recent use, and contextual signals.
- A reusable Qt panel that can be embedded and can expose selected targets to a
  host.
- The default Qt panel now behaves as a lightweight launcher bar: a focused
  horizontal search box with an optional compact result list. It searches
  anchors and Saved Clips, with management controls kept off the default
  surface.
- SumatraPDF jump execution now has a tested command builder for page,
  rectangle, and search locators, launcher activation integration, and
  executable path resolution through `PINLOOM_SUMATRAPDF_PATH`, common install
  paths, or host injection. Rectangle locators scroll to the stored left/top
  coordinate. Pinloom displays a short-lived, click-through highlight over the
  target rectangle after SumatraPDF finishes the jump.

The mismatch is intentional technical debt for the reset:

- `Resource` is still the primary persisted object.
- `Anchor` uses one canonical structured locator model and remains attached to
  its resource container in the current persistence contract.
- Search still indexes resource content and broad source metadata.
- Foreground SumatraPDF file capture is now wired into the Command Window:
  focus an open PDF in SumatraPDF 3.7, press `Shift+Space`, type `k n`, drag a
  same-page rectangle, name the anchor, and save. Pinloom obtains the full PDF
  path and the rectangle's page coordinates through DDE, so the user does not
  need to search or re-import the PDF first.
- Pinloom Clip is now wired into `pinloom_app.exe` as a resident text
  clipboard MVP with SQLite persistence, tray menu, unified `Shift+Space`
  launcher access through ordinary Command Window search plus explicit
  `clip;search` search/insert and `clip;new` save commands, automatic system clipboard text
  capture, temporary history insertion, row timestamps, and explicit Save Clip
  metadata. An optional Obsidian Vault can hold Saved Clips as one Markdown
  note per Clip; Markdown becomes the content source while SQLite remains the
  local search and usage cache.
- Saved Clip action, storage backend, source application, source window, and
  source URI are persisted independently. The `wb` tag selects the explicit
  Open URL action when metadata is saved; execution no longer infers behavior
  from tags at read time.
- Pinloom Inbox is implemented as a local file object capture MVP. It is
  Link-only by default: Pinloom records the original file path and does not
  move or copy user files. Dropping a file on the Command Window or using
  `i n` from a recent Explorer selection saves a searchable Inbox file with
  name, alias, tag, pinned, and default-app launch behavior. Re-saving the
  same path updates the existing Inbox entry instead of creating duplicates.
- The Command Window is the default global `Shift+Space` entry. It restores a
  compact command/search window and focuses one input.

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

Pinloom starts as a resident app with one global shortcut: `Shift+Space` summons
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

For the contextual Saved Clip workflow, configure PowerToys Keyboard Manager
to remap Caps Lock to `F24`. Pinloom treats `F24` as its private Hyper carrier.
The former `Ctrl+Alt+Shift+backtick` carrier remains supported for compatibility:

- With a non-empty text selection exposed through Windows UI Automation or a
  standard Edit/RichEdit control, `F24+S` opens a Save Clip dialog with the
  first non-empty line as the default name. Confirming writes the selected text,
  chosen name, and tags to the Clip index and, when configured, the Obsidian
  archive. Without an Obsidian Vault the Saved Clip remains local. It
  does not send `Ctrl+C` or read the clipboard. Without a readable selection,
  the save is rejected instead of switching actions. Click the Tags field to
  filter and select existing Clip tags; enter a new value and press `+` to
  create and select it.
- `F24+V` opens the lightweight Clip Picker near the captured text caret. It
  searches Saved Clips only. A plain query such as `reference` matches Clip
  names or aliases. A qualified query such as `wb;reference` requires the exact
  `wb` tag and then matches `reference` against the name or aliases. `wb;`
  lists every Clip carrying that tag. Choosing an ordinary Clip restores the
  original foreground control and inserts the latest Obsidian note body there.
  Choosing a `wb` Clip opens its single HTTP/HTTPS URL in the system default
  browser instead of inserting text.
- Pinloom consumes the `S` and `V` triggers while leaving the `F24` carrier key
  available to PowerToys. Applications with custom, inaccessible editors fall
  back to the current mouse position for picker placement.

The explicit command namespaces remain available. Canonical commands use
`domain;action` names. Each segment also accepts an ordered abbreviation, so
`an`, `ar`, `ah`, `ancr`, and `anco` all resolve to `anchor`, while `an;li`
resolves to `anchor;library`. Colon-separated commands remain accepted as a
transition compatibility form. Existing compact aliases such as `c s`, `k n`,
and `i s` remain supported. Type `c` to see Clip commands.
Type `clip;search` to search all insertable Clip rows (temporary history plus Saved
Clips), or `clip;search <query>` to search by name, alias, tag, preview, or content;
Enter inserts the selected row into the foreground app. Type `clip;new` (or `c n`) to choose a
recent temporary clipboard item and save it as a named Saved Clip with tags,
aliases, and pinned state. The tray `Show Clipboard` action routes back to this
same direct Clip Picker. Use `clip;library` or `c l` to open the separate Clip
Library, with Saved Clips, bounded clipboard history, Trash, full-content
preview, action/backend/provenance display, metadata editing,
deletion/restoration, permanent index removal, and Obsidian source-note access.
Its search field uses the same plain `name-or-alias` and `tag;name-or-alias`
rules as `F24+V`. The Alias column is editable on double-click. Clicking the Tag
column opens the existing-tag filter and new-tag creator; edited cells remain
yellow until `Ctrl+S`, then green for the lifetime of the Library window.
When an Obsidian Vault is configured in Settings, `clip;new` atomically
writes the Saved Clip to the configured relative archive directory. External
Markdown edits and renames are synchronized by stable `pinloom_id`; insertion
reloads the latest note body before pasting. In ordinary unified search, press
Right Arrow on an Obsidian-backed Clip and choose `Open source note` to open it
in Obsidian. Generated note filenames use the Clip name only (`Name.md`); name
and alias identifiers are unique across Saved Clips and Clip Trash, compared
case-insensitively. The stable ID remains in YAML frontmatter and is not
displayed in the Obsidian note title.
Deleting an Obsidian-backed Clip writes `pinloom_state: "deleted"` before
moving the SQLite row to Trash. Restoring writes `saved`. Permanent removal
keeps the Markdown file, marks it `forgotten`, and removes only Pinloom's local
index row, so a later vault scan cannot recreate the Clip.

Type `k` or an ordered abbreviation of `anchor` to see anchor commands. Use
`anchor;library` (or `k l`) to open the Anchor Library, which lists every file
with at least one anchor, including archived records in Trash. Search can be
combined with scope, tag, resource type, target application, directory, time,
and usage filters. Saved views preserve those filters. File and anchor tables
support multi-selection, usage columns, and ordered multi-column sorting; hold
Shift while selecting additional sort columns.

File aliases/tags and Anchor aliases/tags are separate fields. File rows show
only file metadata; Anchor rows show only Anchor metadata. In either table,
double-click an Alias cell to edit it or click a Tag cell to filter, select, or
create tags from that metadata scope. Unsaved cells are yellow; `Ctrl+S` saves
every pending file and Anchor inline edit and turns the affected cells green
until the Anchor Library closes. Tags are rendered as distinct color chips and
wrap onto additional lines when needed.

The right pane contains only Preview. Selecting one PDF anchor renders its page
or marked rectangle automatically; click the preview to open the fitted larger
view. Pinloom keeps recent previews in memory and persists rendered PNGs under
its application cache, keyed by the PDF timestamp and locator, with a 512 MB
limit. Right-click a PDF anchor and choose Recapture to replace its rectangle
after comparing the old and new locator.
Batch actions cover tags, Pinned state,
file-record archival and restoration, recursive missing-file discovery, manual
relinking, duplicate Resource merging, and duplicate-anchor cleanup. The
integrity scan reports missing targets, duplicate Resources, duplicate anchors,
and invalid locators. The tag manager renames or removes a tag across the
library, and Undo reverses recent session operations unless an external or
irreversible data change invalidates that history.

Right-click file rows to delete all contained Anchors, or right-click Anchor
rows to recapture a PDF rectangle, delete the selection, or delete every Anchor
in the current file. `Ctrl+A`
selects all Anchor rows and Delete executes the applicable delete action. The
library uses a light work theme, while the Trash button opens a distinct dark
themed view. Its file and Anchor context menus
restore or permanently delete file Alias/Tag metadata and Anchors independently.
Moving records to Trash never deletes the source file. Permanent deletion cannot
be undone and creates an automatic SQLite safety backup when the application
uses its normal repository. Manual archive controls are not exposed. The active
Anchor database is `pinloom.sqlite3` and the Clip database is
`pinloom_clip.sqlite3` under Pinloom's application data directory. Automatic
backups are retained under `backups/anchor-library` and
`backups/clip-library`. Settings can stage a different data directory; Pinloom
copies the complete directory and activates it before opening either database
on the next start. The previous directory is retained, nested paths are
rejected, and a non-empty destination is never overwritten.

The recommended SumatraPDF flow is: open or
focus the target PDF in SumatraPDF, press `Shift+Space`, type `anchor;new` or `k n`,
drag a rectangle inside one PDF page, enter the anchor name plus optional
aliases/tags/pinned state, then save. Right-click or press `Esc` while dragging
mode is active to cancel. The dialog shows the full PDF path, page, rectangle,
zoom, and DDE capture source. Opening the anchor returns to the stored page and
scroll position and briefly highlights the target rectangle. SumatraPDF 3.7 or
newer is required for the `GetFileState()` and `GetMousePos()` DDE requests used
by this workflow.

Type `i` to see Inbox commands. Drop a local file on the Command Window, then
press Enter on `i n` to save it as a Link-mode Inbox file; if no file is
pending, `i n` tries the file selection from the Explorer window that was in
front before `Shift+Space` opened Pinloom. Type `i s <query>` to open the main
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
clipboard text after a successful paste; restoration is delayed until the
target has processed `Ctrl+V` and is skipped if another application changes the
clipboard meanwhile. This can be changed in Pinloom Settings. The same settings
surface contains the app blacklist, sensitive-text
markers, maximum text size, temporary history limit, and history TTL so Clip
history is not an unbounded default store. New profiles exclude common password
manager processes by default, and clipboard events resolve the current
foreground process before applying that blacklist. Obsidian integration uses direct
UTF-8 Markdown file access and the `obsidian://` URI; it does not require a
community plugin. The managed frontmatter fields are `pinloom_id`,
`pinloom_type`, `pinloom_version`, `pinloom_state`, `name`, `aliases`, `tags`,
`action_type`, `pinned`, `source_app`, `source_window_title`, `source_uri`,
`created`, and `updated`; the text after frontmatter is the exact insertion
payload. Saved Clip name and alias identity is enforced by SQLite, while
`clip_identities`, `clip_tags`, and FTS5 indexes provide bounded candidate
queries for the Clip Picker.

## Windows Release Packaging

Build the Release target first, then run
`packaging/windows/Package-Release.ps1`. Packaging stops when Git reports
uncommitted or untracked files. `-AllowDirty` is an explicit diagnostic-only
override, and the generated `BUILD-INFO.txt` records the `-dirty` source state.

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
