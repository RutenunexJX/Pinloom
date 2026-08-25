# Pinloom Plan

This plan tracks work that remains after the launcher, SumatraPDF, Inbox, and
Saved Clip foundations are in place. Product boundaries are defined in
`goal.md`; current usage and build instructions are in `readme.md`.

## Current Baseline

- One resident `pinloom_app.exe` process owns the tray, `Shift+Space` Command
  Window, dedicated `F24+S`/`F24+V` Clip chords, anchor repository, and Clip repository.
- The Command Window searches and acts on Anchors, Saved Clips, Inbox files,
  and local resources through one entry/action protocol.
- Anchors use one canonical model: target application, target file or URI,
  locator type, and locator JSON. SQLite schema version 14 performs a one-time
  migration from older anchor columns without retaining a second runtime
  model.
- SumatraPDF is the only PDF host. DDE supplies the active file, page, zoom,
  and same-page rectangle coordinates; launcher execution restores page and
  position and adds a temporary Pinloom highlight.
- Excel, Word, PowerPoint, and Visio have structured jump executors for existing
  anchors. Unsupported or malformed locators fail explicitly.
- Saved Clips support temporary clipboard history, named persistence, search,
  indexed insertion lookup, local or Obsidian-backed Markdown storage,
  explicit actions/provenance, coordinated Trash state, and contextual
  selected-text capture.
- Inbox captures files and folders as metadata links, optionally copies files
  into managed storage, and registers explicit browsable roots.
- Root Library lazily displays registered roots and permits file/folder metadata
  tagging without recursively indexing root contents. Its default root is
  configurable without a fixed drive assumption, and `_PinloomData` is excluded
  from browsing.
- The Anchor Library provides advanced combined filters and saved views; usage
  sorting; direct jumps; separate file/Anchor metadata; inline Alias/Tag editing;
  cached automatic PDF region preview and context-menu SumatraPDF rectangle
  recapture; batch
  tags/Pinned/lifecycle/path operations; duplicate cleanup; tag and integrity
  management; session Undo; a dedicated Trash workflow; and automatic SQLite
  safety backups without user-facing archive controls.
- Both databases share one configurable local data root. Directory changes are
  staged and copied before database startup. Startup integrity checks protect
  both repositories, and finalized paired snapshots retain both databases with
  one manifest and timestamp.
- Whole-disk crawling, eager root indexing, relationship graphs, a separate Clip
  application process, and JSON Clip archive are not part of the runtime.

## Active Change Set: Command and Native Anchor Capture

The following work is one Pinloom delivery. Word, Visio, and Excel capture are
part of this change set and are not deferred to a later phase.
The widget architecture, visual system, native-capture boundaries, and focused
verification are specified in [`UI_IMPLEMENTATION_PLAN.md`](UI_IMPLEMENTATION_PLAN.md).
Implementation is complete and the configured test suite passes 7/7;
packaging was not run.

### Command Window and capture confirmation

- Remove whitespace-based legacy command forms (`c s`, `c n`, `c l`, `k n`,
  `k l`, `i n`, `i s`, and `r l`) from parsing, help text, and tests.
- Keep semicolon commands as the canonical grammar, including abbreviated
  domains such as `c;search` and `k;new`. Keep colon parsing only as transition
  compatibility.
- Add a compact quick-action row immediately below the `Shift+Space` input.
  It remains visible in the empty/compact state and provides at least
  `Rectangle Anchor` and `Text Anchor`. The current-app Anchor action remains
  available through `anchor;new` and may also be shown contextually.
- Snapshot the foreground application and native target before the Command
  Window takes focus. A quick action must act on that remembered target rather
  than Pinloom's own window.
- After every successful locator capture, open one shared Anchor confirmation
  dialog populated with the captured target and locator. The user can edit the
  name, aliases, tags, and Pinned state. Cancel creates no Anchor; no capture
  path may bypass this dialog and save directly.

### SumatraPDF rectangle and selected-text anchors

- `Rectangle Anchor` hides the Command Window, activates the remembered
  SumatraPDF window, starts rectangle capture immediately, and then opens the
  shared confirmation dialog.
- `Text Anchor` captures the selected PDF text and active file/page before
  opening the confirmation dialog. Its structured locator uses
  `sumatrapdf.search` and stores the selected text, page hint, occurrence or
  surrounding context, and a rectangle fallback when available. Repeated text
  must not silently resolve to an unrelated occurrence.
- A jump is successful only after Pinloom verifies that the intended PDF file,
  page, zoom, and scroll target are active. Replace the fixed post-launch delay
  with bounded DDE/state polling and explicit retry or failure reporting.
- Replace the 1.4-second screen-space flash with a document-scoped highlight
  manager keyed by SumatraPDF window/document and Anchor. Highlights remain
  active until the target PDF document or window closes; multiple Anchors in
  one open PDF may coexist.
- Recalculate visible highlight geometry when the SumatraPDF window moves or
  resizes and when page, zoom, or scroll state changes. Hide an off-screen or
  wrong-page highlight instead of leaving it over unrelated content, and show
  it again when its region becomes visible.

### Word, Visio, and Excel capture in this change set

- Dispatch `anchor;new` from the remembered foreground application. Unsupported
  applications fail explicitly and do not create a page-1 or generic fallback
  Anchor.
- Word: capture the active document and current selection/caret as a
  `word.bookmark` locator. Reuse an applicable existing bookmark when possible;
  otherwise create a Pinloom-owned bookmark through Word COM. Creating or
  saving a bookmark changes the user document and therefore requires explicit
  user authorization. Read-only, protected, or unsaved documents fail with a
  precise reason rather than a weak locator.
- Visio: require one selected shape and capture the document path, page `NameU`,
  and shape UniqueID as `visio.shape`. If a persistent UniqueID must be created,
  treat that as a document mutation with the same authorization rule. Empty or
  multi-shape selections produce a clear corrective message.
- Excel: capture the active workbook, worksheet, and selected range. Prefer an
  existing defined name (`excel.name`) that exactly identifies the selection;
  otherwise store the worksheet and absolute A1 range (`excel.range`). Offer a
  Pinloom-owned defined name only as an explicitly authorized, mutation-stable
  option. Read-only, protected, or unsaved workbooks fail explicitly when the
  requested locator cannot be persisted safely.
- All three adapters use the same shared confirmation dialog and the canonical
  `Anchor` model; application-specific state remains inside `locator_json`.

### Verification and exit criteria

- Whitespace legacy forms are no longer interpreted as commands, while
  canonical semicolon forms and documented abbreviations continue to work.
- Both PDF quick actions preserve the original foreground target, always show
  the confirmation dialog after capture, and save aliases/tags/Pinned state
  only after confirmation.
- Repeated cold-start and reused-instance PDF jumps land on the intended file,
  page, and region without intermittent page-1 success reports.
- PDF rectangle highlights survive focus, move, resize, zoom, scroll, and page
  changes as specified, and are removed when the target PDF closes.
- Word `.docx` bookmark, Visio `.vsdx` shape, Excel `.xlsx` range, and Excel
  defined-name capture/jump round trips pass against real application instances.
  Cancellation, no selection, read-only, protected, and unsaved-document cases
  create no misleading Anchor.
- Core, SQLite, capture, command-parser, widget, and native-adapter tests pass
  from a clean build before this change set is committed or packaged.

## Phase 1: Daily-Use Hardening

- Make command, capture, insertion, and jump failures diagnosable without
  modal error loops.
- Preserve foreground-window and insertion-target behavior across repeated
  Command Window use.
- Exercise schema migration against a copy of an existing personal database
  before packaging.
- Keep search and action latency stable as anchor and Clip counts grow.

Exit criteria:

- Existing schema versions upgrade to version 14 without losing resources,
  aliases, tags, anchors, or usage ranking.
- Core, SQLite, Clip, capture, and widget suites pass from a clean build.
- Normal launcher and Hyper workflows do not require a second picker window.

## Phase 2: Native Capture Depth

- Add native locator capture only where the owning application exposes a
  stable and legally usable interface.
- Prefer workbook range, Word bookmark, PowerPoint shape, and Visio UniqueID
  locators over screen coordinates or inferred text.
- Keep manual structured locator creation as an explicit fallback when native
  capture is unavailable.

Exit criteria:

- Each new capture path has a matching jump round trip and a deterministic
  locator contract.
- Failure does not create a misleading page-1 or generic-location anchor.

## Phase 3: Clip Reliability

- Keep the direct Clip Picker distinct from command syntax and provide a
  separate Clip Library for Saved, History, and Trash management.
- Use semicolon-separated canonical commands while retaining colon parsing as
  transition compatibility.
- Improve UI Automation selection coverage without synthesizing `Ctrl+C`.
- Preserve Obsidian note identity across edits and renames.
- Keep clipboard restoration race-safe and observable through diagnostics.
- Add richer payload kinds only after the plain-text contract remains stable.

Exit criteria:

- `F24+S` consistently captures accessible selected text and requests Clip
  name/tags; `F24+V` consistently opens Saved Clip retrieval at the insertion
  target.
- Saved Clip insertion reads the latest external Markdown body.
- Temporary history remains bounded by policy and excluded applications.

## Phase 4: Distribution

- Produce a repeatable Windows package containing the required Qt runtime and
  Pinloom executable.
- Validate first-run settings, SumatraPDF discovery, database initialization,
  single-instance behavior, and uninstall behavior.
- Add operating-system startup only as an explicit user setting.

Exit criteria:

- A clean Windows user profile can install, launch, capture, search, jump,
  insert, upgrade, and uninstall without source-tree dependencies.

## Engineering Guardrails

- Do not reintroduce dual anchor models or application-specific fields into
  `Anchor`.
- Do not add broad file-content indexing, OCR, semantic search, web/feed
  importers, or relationship graphs as v1 headline behavior.
- Keep application launchers behind structured locator validation.
- Treat user files, PDFs, Office documents, Obsidian notes, and Inbox source
  files as externally owned data; Pinloom metadata deletion must not delete
  them.
- Scale tests with behavioral risk and run GUI tests with an installed Qt
  platform plugin (`windows` in the current local toolchain).
