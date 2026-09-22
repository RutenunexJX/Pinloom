# Pinloom

Current version: `0.4.4`; database Schema: `16`.

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

Pinloom does not crawl the whole disk or eagerly index every file under a root.
Registered roots provide an explicit browsing boundary for user-authored file
metadata; relationship-graph storage remains outside the runtime.

## Priority Executors

1. SumatraPDF
   - The first implementation of the viewer-neutral `PdfViewerAdapter`
     boundary. Capture, activation, viewer IPC, coordinate conversion, launch,
     navigation verification, cancellation, and timeout handling stay inside
     this adapter.
   - Supports page, text search, and rectangle-derived scroll targets through
     SumatraPDF command-line arguments. Stored legacy zoom values remain
     readable, but new locators do not persist live viewer zoom.
   - Text PDFs and scanned PDFs are treated the same: the user names an anchor
     against a foreground or selected PDF context and Pinloom stores page plus
     rectangle.
   - The SumatraPDF adapter uses SumatraPDF 3.7 DDE. When
     `Shift+Space` is pressed, Pinloom remembers the foreground SumatraPDF
     window and obtains the active PDF identity, page, and runtime view state.
   - Anchor Library Preview uses the official SumatraPDF 3.7
     `sumatrapdf-tool.exe` beside `SumatraPDF.exe` to render a page directly.
     Rectangle anchors are cropped to their marked content with a small context
     margin; Preview does not open or screenshot the reader window.
   - `anchor;new` hides the Command Window and opens a viewer-neutral capture
     layer over the adapter-provided window geometry. Drag a rectangle inside
     one PDF page; right-click or press `Esc` to cancel. The adapter converts
     screen positions to PDF page coordinates and records page geometry.
   - Example locator:

```json
{"type":"sumatrapdf.rect","version":3,"document":{"identity":"E:/docs/clock.pdf"},"page":12,"rect":[420,860,780,920],"unit":"pt","coordinateSpace":"page-top-left","rotation":0,"mediaBox":[0,0,612,792],"cropBox":[0,0,612,792],"userUnit":1,"source":"sumatrapdf-adapter-region","provenance":{"adapter":"sumatrapdf","source":"sumatrapdf-adapter-region"}}
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
- PDF operations now enter through `PdfViewerAdapter`. The SumatraPDF adapter
  owns foreground discovery, DDE state, page-coordinate sampling, launch, and
  navigation verification; Command Window, library, host, and deep-link paths
  use the same dispatcher/open-service route. Rectangle anchors use an
  annotated-copy presenter, so the mark is part of a temporary PDF preview
  rather than a desktop overlay.
- The Qt surfaces share semantic light/dark tokens for canvas, panels, text,
  selection, focus, success, warning, and error. Layouts use a 4 px base grid,
  28/32/36 px control roles, visible keyboard focus, and system reduced-motion
  preferences. Anchor, Clip, and Inbox command namespaces retain distinct,
  contrast-checked accents without using gradients.

The mismatch is intentional technical debt for the reset:

- `Resource` is still the primary persisted object.
- `Anchor` uses one canonical structured locator model and remains attached to
  its resource container in the current persistence contract.
- Search still indexes resource content and broad source metadata.
- Foreground SumatraPDF file capture is now wired into the Command Window:
  focus an open PDF in SumatraPDF 3.7, press `Shift+Space`, type `anchor;new`, drag a
  same-page rectangle, name the anchor, and save. Pinloom obtains the full PDF
  path and the rectangle's page coordinates through DDE, so the user does not
  need to search or re-import the PDF first.
- Pinloom Clip is now wired into `pinloom_app.exe` as a resident text
  clipboard MVP with SQLite persistence, tray menu, unified `Shift+Space`
  launcher access through ordinary Command Window search plus explicit
  `clip;search` search/insert and `clip;new` save commands, opt-in system clipboard text
  capture, temporary history insertion, row timestamps, and explicit Save Clip
  metadata. An optional Obsidian Vault can hold Saved Clips as one Markdown
  note per Clip; Markdown becomes the content source while SQLite remains the
  local search and usage cache.
- Saved Clip action, storage backend, source application, source window, and
  source URI are persisted independently. The `wb` tag selects the explicit
  Open URL action when metadata is saved; execution no longer infers behavior
  from tags at read time.
- Pinloom Inbox accepts local files and folders. A file can remain at its
  original path or be copied into Pinloom-managed storage; a folder can be
  tagged as one item or registered as a browsable root. Each item supports
  name, aliases, tags, pinned state, search, and default-app launch. Re-saving
  the same stored path updates the existing item instead of creating a duplicate.
- The Command Window is the default global `Shift+Space` entry. It restores a
  compact command/search window and focuses one input.

The next implementation phases should converge these foundations toward the v1
anchor model instead of expanding source indexing.

## Host Integration

Pinloom exposes a user-local, versioned `pinloom-host/v1` bridge for trusted
desktop applications such as ZeroSlack. The bridge supports bounded
`capabilities`, `search`, `resolve`, and `open` requests over a local Qt socket.
Search uses `PinloomEntrySearchService`; resolution reads the authoritative
repository or Saved Clip source; opening delegates to `PinloomOpenService`.
Consumers receive stable entry/resource/anchor/clip identities and
`pinloom://` URIs, not direct database access.

Starting `pinloom_app.exe --hidden` keeps the resident application available
without opening a primary window. The bridge is limited to the current user,
rejects unsupported protocol versions and oversized requests, and does not
expose mutation commands.

## Build

The first verified local toolchain is Qt 6.10.2 with MinGW and CMake/Ninja from
`E:\QT6`.

```powershell
$env:PATH='E:\QT6\Tools\mingw1310_64\bin;' + $env:PATH
E:\QT6\Tools\CMake_64\bin\cmake.exe -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=E:\QT6\6.10.2\mingw_64 -DCMAKE_MAKE_PROGRAM=E:\QT6\Tools\Ninja\ninja.exe -DCMAKE_CXX_COMPILER=E:\QT6\Tools\mingw1310_64\bin\g++.exe
E:\QT6\Tools\CMake_64\bin\cmake.exe --build build
E:\QT6\Tools\CMake_64\bin\ctest.exe --test-dir build --output-on-failure
```

The application version is defined by `project(VERSION ...)` in
`CMakeLists.txt`. It is shown in the Command Window and diagnostics and is also
written to the portable release README. Windows release packaging recreates one
fixed `Pinloom` directory so shortcuts can continue to target
`Pinloom\pinloom_app.exe` after an update.

## Run

```powershell
.\build\pinloom_app.exe
```

Pinloom starts as a resident app with one global shortcut: `Shift+Space` summons
the Command Window and focuses one search/command input. When another application
takes foreground, the Command Window collapses to a non-activating floating
toolbar with three Capture icons. Hover/focus does not expand it; `Shift+Space`
restores the full bar, after any active floating capture/confirmation finishes.
The version remains in the title bar, not beside Navigate. Type an ordinary query
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
transition compatibility form. Whitespace-separated legacy forms are ordinary
unified-search text and are not interpreted as commands. Type `c` to see Clip commands.
Each command has one stable registry ID and one canonical visible row. Input
aliases are accepted by the parser but are not rendered as extra commands.
Command rows, quick-action buttons, the Pinloom menu, and tray Settings and
Diagnostics actions execute through the same dispatcher. Dispatcher results
distinguish completed, canceled, and failed operations.
Type `clip;search` to search all insertable Clip rows (temporary history plus Saved
Clips), or `clip;search <query>` to search by name, alias, tag, preview, or content;
Enter inserts the selected row into the foreground app. Type `clip;new` to choose a
recent temporary clipboard item and save it as a named Saved Clip with tags,
aliases, and pinned state. The tray `Show Clipboard` action routes back to this
same direct Clip Picker. Use `clip;library` to open the separate Clip
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
Use `clip;pdf-text`, or the `PDF Text Clip` button below the command input, to
save the selected text from the remembered SumatraPDF document through the
same metadata confirmation and Saved Clip persistence path as `F24+S`.

Type `anchor` or an ordered abbreviation such as `an` to see anchor commands. Use
`anchor;library` to open the Anchor Library, which lists every file
with an anchor or user-authored file metadata, including archived records in Trash. Search can be
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
in the current file. Active file rows disappear when no active Anchor, file Tag
or file Alias remains; pinned/explicitly-retained state alone does not keep a
row visible. This hides the row without deleting the source or Trash records.
Unchanged legacy naming conflicts do not block removing Anchors; creating or
restoring conflicting names/aliases remains rejected. `Ctrl+A`
selects all Anchor rows and Delete executes the applicable delete action. The
library follows the current system light or dark scheme. Trash remains in that
scheme but uses the shared error surface and destructive accent, so changing
scope does not invert the entire interface. Its file and Anchor context menus
restore or permanently delete file Alias/Tag metadata and Anchors independently.
Moving records to Trash never deletes the source file. Permanent deletion cannot
be undone and creates an automatic SQLite safety backup when the application
uses its normal repository. Manual archive controls are not exposed. The active
Anchor database is `pinloom.sqlite3` and the Clip database is
`pinloom_clip.sqlite3` under Pinloom's application data directory. Startup
performs SQLite integrity checks and retains paired database snapshots under
`backups/application-data`; each finalized snapshot contains a manifest and
both databases when Clip is available. Destructive Anchor Library operations
also keep operation-specific safety backups under `backups/anchor-library`.
Settings can stage a different data directory. On the next start, Pinloom
copies the current data into an empty destination, or adopts a non-empty
directory that already contains both `pinloom.sqlite3` and
`pinloom_clip.sqlite3` without overwriting either database. The previous
directory is retained, nested paths and unrelated non-empty destinations are
rejected. The data directory is local application state, not a synchronization protocol: two running
computers must not open copies managed by a live file-sync service. For
multi-computer Saved Clips, synchronize the configured Obsidian Markdown vault
and let each computer keep its own local SQLite index. Anchor replication is
not implemented.

The recommended SumatraPDF flow is: open or
focus the target PDF in SumatraPDF, press `Shift+Space`, type `anchor;new`,
drag a rectangle inside one PDF page, enter the anchor name plus optional
aliases/tags/pinned state, then save. Right-click or press `Esc` while dragging
mode is active to cancel. Capture also has a bounded timeout and reports
cross-page, coordinate sampling, and timeout failures explicitly. The dialog
shows the full PDF path, page, rectangle, runtime zoom, and capture source.
During a drag, target checks run outside the UI event loop. DDE requests run in
the bounded `pinloom_pdf_probe.exe` helper, which must remain beside the main
executable. A hung probe is terminated without terminating the reader; Escape
and selection painting remain responsive. `--package-check` verifies the helper
without opening user data or contacting a PDF reader.
Opening the anchor returns to the stored page and rectangle. Version 3 locators
store document identity, page-space coordinates, media/crop boxes, rotation,
user unit, and adapter provenance with `coordinateSpace: page-top-left`; they
persist neither desktop pixels nor live zoom. Existing locators, including
legacy zoom/rectangle spellings, remain readable and are not rewritten in the
background. A successful edit or recapture writes the current version.
Rectangle
jumps generate a temporary annotated PDF copy and launch it only after the
expected preview file and page are verified. The original PDF is not modified,
and the annotation remains attached to PDF content when the viewer scrolls,
zooms, or moves. Generation and navigation are asynchronous and bounded;
superseded or canceled requests cannot launch stale previews. SumatraPDF 3.7
or newer is required for the DDE requests used by capture.

The command window keeps `PDF Rectangle Anchor`, `PDF Text Anchor`, and
`PDF Text Clip` actions directly below the `Shift+Space` input. Text Anchor
captures selected SumatraPDF text as a page-hinted `sumatrapdf.search` locator.
The same operations are available as `anchor;rectangle`, `anchor;text`, and
`clip;pdf-text`. `anchor;new` also dispatches to the
remembered Word, Visio, or Excel window: Word uses bookmarks, Visio uses a shape
UniqueID, and Excel uses an exact defined name or absolute worksheet range.
Any required document mutation is shown in the shared confirmation dialog and
is performed only after explicit authorization.

Type `i` to see Inbox commands. Dropping a local file or folder on the Command
Window immediately opens its metadata dialog. For a file, choose whether it
stays at its original path or is copied into Pinloom-managed storage. For a
folder, choose whether to tag only that folder or register it as a root. Use
`inbox;new` for a pending item or the selection from
the Explorer window that was in front before `Shift+Space` opened Pinloom. Type
`inbox;search <query>` to search Inbox items.
Dropping plain text opens the Saved Clip metadata dialog instead.

Use `root;library` (or `r;l`) to open Root Library. It lists all registered
roots and lazily browses their files and folders without eager recursive
indexing; selecting an item allows its name, aliases, and file tags to be saved.
Choose the default root directory in Pinloom Settings; no drive letter is
assumed. On profiles without an explicit setting, an application data directory
named `_PinloomData` causes its parent directory to be inferred once as the
default root. `_PinloomData` and everything below it are excluded from Root
Library browsing. Changing the default keeps the previous directory as an
ordinary root, while the current default root cannot be removed.

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
override. The script produces only a directly runnable `Pinloom` directory with
the required Qt/MinGW runtime and SQLite driver. It does not create an installer,
ZIP archive, checksum manifest, launcher script, or user database.
The generated `qt.conf` confines Qt plugin lookup to the deployed runtime.

`E:\Pinloom\artifacts\Pinloom` is a staging artifact, not the installed release.
The formal package is `E:\PinloomRoot\AppPackage\AppSuite\Apps\Pinloom`.
Back up that exact directory before replacing it, preserve its runtime license
notices, and update only Pinloom's component/version and checksum entries in the
suite metadata. Other application/runtime directories and user data stay intact.

Run `pinloom_app.exe --package-check` to validate the deployed Qt/UI runtime
and an in-memory SQLite connection. This check exits without opening user
settings/databases, contacting the resident instance, showing a window, or
registering clipboard/hotkey hooks. The process exit code is zero on success.

Internally, the Command Window routes ordinary work through
`registry command ID -> dispatcher -> structured result`. Commands search
`PinloomEntry` objects; execution returns a completed, canceled, or failed
state plus `message`, `diagnostics`, and a `next UI hint`. The Qt UI is one
caller of that protocol.

## Suite application protocol

Pinloom is the authoritative `suite-app/v1` owner of `pinloom://` resources.
Canonical stable object links are `pinloom://anchor/<stable-id>` and
`pinloom://clip/<stable-id>`; legacy `pinloom://entry/...` links remain
read-compatible for File/Resource and existing integrations. Passing a link
to `pinloom_app.exe` opens it in the primary resident instance. Malformed,
missing, and deleted object links report explicit errors; deleted objects must
be restored before opening. Pinloom exposes `pinloom.entry.open` and
`pinloom.source-anchor.create`, plus the model Surface
`pinloom.entry.preview`. Source-anchor creation goes through Pinloom's existing
repository and host callbacks; another application never reads the Pinloom
database directly.

The optional neutral Runtime is discovered through the shared SuiteApp SDK.
Pinloom registers after its host callbacks are ready and returns protocol
responses before opening modal preview UI, so nested Qt event loops cannot
block callers. If the Runtime is absent, Pinloom's command, anchor, and clip
workflows remain independently usable.


## Native capture and UI maintenance contract

Capture remembers the foreground target before the Command Window takes focus. A successful capture opens
the shared confirmation dialog for name, aliases, tags and pinned state; cancellation creates no Anchor.
Word bookmark creation/saving, Visio UniqueID creation and optional Excel defined-name creation require
explicit authorization when they mutate a native document. Unsupported, unsaved or protected targets fail
with a reason instead of producing a generic locator. Excel prefers a matching defined name or exact A1 range.

PDF capture goes through `PdfViewerAdapter`; identity, page-space coordinates, rotation and crop metadata are
authoritative. The annotated-copy presenter supplies stable highlights. Window geometry is an observation,
not persisted Anchor identity, and superseded/canceled capture cannot open a stale preview.

Semantic light/dark colors, focus, selection, status and density are shared across command, library,
settings and confirmation surfaces. State is expressed with text as well as color; native-app launch and
capture failures retain an actionable explanation near the relevant operation.

## Control backends

Release 0.4.3 defaults to `-DPINLOOM_UI_BACKEND=ELA`.
Ela provides actual buttons, tool buttons,
line edits, choices, checkboxes, numeric inputs, menus, text previews, scroll
areas, resident bars, tree/list/table views, labels, notifications, window chrome
and compact navigation. Tables and lists use independent QStandardItemModels;
tag pickers retain filtering, creation and deferred edits. ElaAppBar is composed
with Qt windows/dialogs to preserve their lifecycle, including hide-to-tray.
Settings use Ela cards and immediate advanced drawers; tag popups share themed
frames, and managed tooltips retain Qt help events, explicit tooltip data and
screen bounds. Settings and manual PDF forms keep their action footer visible
while advanced fields scroll; collapsed settings retain their values.
Business models, PDF surfaces and native safety/file dialogs remain Pinloom/Qt-owned.
See [the migration inventory](docs/ELA_MIGRATION.md).

The vendored dependency is pinned to upstream commit
`454cac2d57a47d3cc28577dc817793aec1881ca7` plus recorded compatibility patches.
Build with Qt **6.10.2** (including private headers) and the existing MinGW 13.1
toolchain. No other application's source or build directory is required.
Configure a fresh build directory, for example:

```text
cmake -S . -B build-ela-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DPINLOOM_UI_BACKEND=ELA
cmake --build build-ela-release --parallel 3
ctest --test-dir build-ela-release --output-on-failure
```

`SUITEUI` and `CLASSIC` are mutually exclusive compile-time alternatives.
`SUITEUI` still requires `SuiteUi 0.1.1 EXACT` via `SuiteUi_DIR`. Remove the old
`PINLOOM_ENABLE_SUITEUI` cache entry when changing backends. Before startup,
`PINLOOM_UI_STYLE=classic` selects native fallback controls; `ela` or `suiteui`
must match the compiled backend. Switching renderers within a process is not
supported. Light/dark switching retains the same control instances and geometry.
Ela menu/input/combo, navigation, drawer and notice transitions are immediate; reduced-motion settings remain
available for the other backends.

QSS is scoped away from Ela interactive-control and item-view painting; semantic
labels and window surfaces retain Pinloom's colors, typography and notice roles.
The offscreen Ela tests cover real surfaces, dialog acceptance/cancellation,
keyboard behavior, popup teardown and light/dark previews at four scale factors.
They do not certify live external PDF capture, real tray interaction or mixed-
monitor DPI. Existing settings, libraries, identity rules and schema are unchanged.

Ela builds stage required MIT and Font Awesome Free Solid (SIL OFL 1.1) notices
under `notices/ElaWidgetTools`. The release script verifies and copies the Ela
DLL and notices; SuiteUi builds retain their installed SDK notices. This source
migration is included in 0.4.3; release packages require an explicit clean-source build.
