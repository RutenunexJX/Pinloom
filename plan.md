# Pinloom Plan

This plan supersedes the previous general content-indexing roadmap. Existing
reader and indexing work is retained only as infrastructure or compatibility
unless a later phase explicitly pulls it into the anchor launcher loop.

## Phase 0: Product Specification Reset

Goal: reset the documented product direction to a Listary-style deterministic
anchor launcher.

Scope:

- Update `goal.md`, `plan.md`, and `readme.md`.
- State that Anchor is the primary entity.
- State that files/resources are target containers.
- State that v1 search is anchor name, alias, tag, and target metadata search.
- Mark general content indexing, special reader expansion, screenshot search,
  semantic search, OCR search, and heavy library UI as non-mainline for v1.
- Build, test, commit, and push.

Status:

- Repository inspection: done.
- Current implementation versus new direction: documented in `readme.md`.
- Documentation reset: done.
- Build/test: required before the phase commit.
- Commit/push: required for phase closure.

## Phase 1: Anchor Data Model Convergence

Goal: make the persisted model match the product model.

Target model:

```text
id, name, target_app, target_file/target_uri, locator_type, locator_json,
aliases, tags, pinned, created_at, updated_at, used_at
```

Scope:

- Introduce a first-class anchor repository surface.
- Preserve compatibility with existing resource-attached anchors.
- Store structured locators for native app dispatch.
- Keep aliases, tags, FTS, ranking, recent use, and pinned signals.
- Update SQLite and in-memory repositories together.
- Add migration tests and repository tests.

Search ranking:

```text
exact name > alias > tag > recent/pinned > target metadata
```

Status:

- Anchor structure extended with first-class locator fields while preserving
  the resource-attached compatibility API.
- SQLite migration v8 adds anchor locator columns without rebuilding existing
  tables or deleting legacy anchor data.
- In-memory and SQLite repositories both persist, hydrate, and search anchor
  name, aliases, tags, and explicit target metadata.
- Legacy anchors still read through `type`/`target`/line/page/region fallback.
- Build and full `ctest --test-dir build --output-on-failure` passed for the
  phase implementation.

## Phase 2: Listary-Style Overlay UI

Goal: make the main experience a lightweight command palette.

Scope:

- Replace the resource-library dashboard with a compact overlay.
- Show one search box and a dense result list.
- Result rows show anchor name, target app/file, locator summary, tag chips,
  and alias hints.
- Keyboard shortcuts:
  - Enter: jump.
  - Ctrl+K: capture current app position.
  - Alt+A: add alias.
  - Alt+T: add tag.
  - Ctrl+E: edit anchor.
  - Delete: delete anchor.
- Keep folder/index controls out of the default v1 surface.

Status:

- Default `PinloomPanel` surface now opens as a launcher-style command palette:
  search box first, compact result list second, and library/root management
  hidden behind an explicit Manage toggle.
- Anchor result rows prioritize anchor display name, target app/file/uri,
  locator summary, tags, and aliases while preserving resource-result
  compatibility.
- Enter activation, Ctrl+K capture placeholder, Alt+A alias, Alt+T tag,
  Ctrl+E lightweight anchor edit, and Delete deletion placeholder are covered
  by widget tests.
- Build and full `ctest --test-dir build --output-on-failure` validation
  passed for the phase implementation.

## Phase 3: PDF-XChange Jump Executor

Goal: jump to manually entered PDF-XChange locators.

Scope:

- Use PDF-XChange Editor as the unified PDF host.
- Accept file plus page, rect, zoom, unit, and optional highlight fields.
- Generate PDF-XChange command-line actions such as:

```text
/A "page=...;zoom=...;highlight=...;usept=yes" "file.pdf"
```

- Support viewrect mode where appropriate.
- Do not use SumatraPDF as the main v1 path.
- Do not depend on OCR for scanned PDFs.

Acceptance:

- A manually entered locator can open a scanned or text PDF at the specified
  page and region, with highlighting when available.

Status:

- PDF-XChange command building is implemented in core and covered by tests for
  `pdfxchange.rect`, legacy `PdfPage` fallback, and missing target paths.
- Launcher activation dispatches PDF-XChange/pdf/pdfxchange anchors through the
  executor before generic URL/file fallback, while preserving host handler
  priority.
- Executable lookup supports `PINLOOM_PDFXCHANGE_PATH`, common Windows install
  paths, and widget-level injection for tests or embedding.
- Command construction, environment path resolution, and UI activation status
  are covered by automated tests; end-to-end GUI verification requires a local
  PDF-XChange install and is documented in `docs/pdfxchange_validation.md`.
- Build and full `ctest --test-dir build --output-on-failure` passed again for
  the validation hardening changes.

## Phase 4: PDF Anchor Capture

Goal: let users create PDF page and rectangle anchors from their current
workflow.

Scope:

- Research whether PDF-XChange exposes current page, selection, annotation, or
  rectangle coordinates reliably.
- If native capture is unreliable, implement a Pinloom calibration mode only for
  coordinate capture. This mode must not become a PDF reader.
- Store page and rectangle locators compatible with the Phase 3 executor.

Acceptance:

- The user can create a PDF anchor, search it, and jump back to the same region.

Status:

- The PDF UX now treats `Ctrl+K` and the PDF `Add Anchor` path as
  `Capture PDF Anchor`: users enter name, aliases, tags, and pinned state while
  the locator comes from the selected PDF context.
- The current supported capture is explicitly a selected-PDF fallback. If a PDF
  resource or PDF-XChange anchor is selected, Pinloom uses that file and falls
  back to page 1 plus an approximate full-page rectangle when no native current
  view/selection data is available.
- Raw file/page/coordinate entry remains available only as an advanced/debug
  fallback and is no longer the default user path.
- No-PDF-context attempts now report that a PDF must be opened or selected
  before capture.
- This slice intentionally does not read PDF-XChange's current view, current
  page, selection, annotations, or live coordinates. Native PDF-XChange capture
  and any calibration mode remain research items for a later Phase 4 step.

## Phase 5: Office And Visio Executors

Goal: support deterministic jumps into common engineering documents.

Scope:

- Excel: file plus sheet plus range or named range.
- Word: file plus bookmark.
- PowerPoint: file plus slide plus shape id or shape name.
- Visio: file plus page plus shape UniqueID.
- Implement jump executors first, then capture.

Status:

- Excel jump executor skeleton and launcher dispatch are implemented: anchors
  with `excel.range` and `excel.name` locators now build a deterministic
  PowerShell/COM command object and route through an injectable Excel launcher
  before generic file fallback.
- Visio jump executor skeleton and launcher dispatch are implemented: anchors
  with `visio.shape` locators now build a deterministic PowerShell/COM command
  object for document, page, and shape UniqueID jumps and route through an
  injectable Visio launcher before generic file fallback.
- Word jump executor skeleton and launcher dispatch are implemented: anchors
  with `word.bookmark` locators now build a deterministic PowerShell/COM
  command object for document and bookmark range jumps and route through an
  injectable Word launcher before generic file fallback.
- PowerPoint jump executor skeleton and launcher dispatch are implemented:
  anchors with `powerpoint.shape` locators now build a deterministic
  PowerShell/COM command object for presentation, slide, and shape id/name
  jumps and route through an injectable PowerPoint launcher before generic file
  fallback.
- Phase 5 jump executor coverage now includes Excel, Word, PowerPoint, and
  Visio.
- Manual Excel anchor capture is now implemented in core: explicit name, file,
  sheet/range or named range, aliases, tags, pinned, source, and target app
  inputs are normalized through a manual Excel capture provider, saved as
  searchable workbook anchors, and remain compatible with the Excel executor's
  `excel.range` and `excel.name` command builder. `Ctrl+K` can route through an
  injectable manual Excel request provider; native Excel current-selection
  capture and a default Excel dialog remain pending.
- Manual Visio anchor capture is now implemented in core: explicit name, file,
  page, shape UniqueID, aliases, tags, pinned, source, and target app inputs are
  normalized through a manual Visio capture provider, saved as searchable Visio
  file anchors, and remain compatible with the Visio executor's `visio.shape`
  command builder. `Ctrl+K` can route through an injectable manual Visio request
  provider. This is manual/injectable Visio capture only; native current-shape
  capture through live Visio COM remains pending.
- Manual Word bookmark anchor capture is now implemented in core: explicit
  name, file, bookmark, aliases, tags, pinned, source, and target app inputs are
  normalized through a manual Word bookmark capture provider, saved as
  searchable Word file anchors, and remain compatible with the Word executor's
  `word.bookmark` command builder. `Ctrl+K` can route through an injectable
  manual Word request provider. This is manual/injectable Word bookmark capture
  only; native current-selection capture and automatic bookmark insertion
  through live Word COM remain pending.
- Manual PowerPoint shape anchor capture is now implemented in core: explicit
  name, file, slide, shape id or shape name, aliases, tags, pinned, source, and
  target app inputs are normalized through a manual PowerPoint shape capture
  provider, saved as searchable PowerPoint file anchors, and remain compatible
  with the PowerPoint executor's `powerpoint.shape` command builder. `Ctrl+K`
  can route through an injectable manual PowerPoint request provider. This is
  manual/injectable PowerPoint shape capture only; native current-selection
  capture through live PowerPoint COM remains pending.

## Phase 6: Experience Completion

Goal: make the launcher dependable for daily use.

Scope:

- Tray resident process.
- Global hotkey.
- External application path settings.
- Anchor import/export.
- Broken-anchor detection.
- Recent and pinned anchor polish.
- Fast alias/tag editing.

Status:

- External application path settings skeleton is implemented in core via
  launch settings for PDF-XChange and shared PowerShell automation paths.
- PDF-XChange, Excel, Word, PowerPoint, and Visio executors can consume the
  injected settings while preserving existing default/legacy builder behavior.
- `PinloomPanelOptions` can host-inject the launch settings, and launcher
  activation passes the configured paths through to the command builders.
- This is intentionally not a complete settings UI or persistence layer.
- Build and full `ctest --test-dir build --output-on-failure` passed for this
  first Phase 6 slice.
- Broken-anchor detection now has a core `AnchorHealthCheck` skeleton for
  static, non-launching checks of local target existence, configured launcher
  paths, non-local targets, and unsupported locators.
- This is intentionally not yet wired into realtime UI scanning, background
  monitoring, or a persisted health cache.
- Anchor import/export now has a core `AnchorArchive` skeleton for explicit
  JSON file paths using schema `pinloom.anchors` v1. Export is driven by a
  caller-provided resource list and only writes resource containers that carry
  anchors plus anchor fields. Import conservatively skips resources whose ids
  already exist instead of overwriting local resources.
- This is intentionally not yet connected to UI, tray actions, app data
  defaults, or remote sync.
- Pinloom Clip's resident text runtime is now integrated into `pinloom_app.exe`
  with the shared app-data directory, `pinloom_clip.sqlite3`, default
  `Ctrl+Shift+V`, tray actions for Show Clipboard, Pause/Resume Capture, and
  Quit, temporary text history insertion, and a picker action to save a
  temporary clip as a named Saved Clip.
- Saved Clips are searchable inside the Clip picker by name, alias, and tag.
  Main Pinloom anchor search does not yet index Saved Clips as first-class
  launcher results.

## Frozen Or Demoted Work

The following areas are no longer product mainline for v1:

- Broad directory content indexing as the headline experience.
- Web/feed/history/bookmark import expansion.
- OCR search and screenshot search.
- Semantic search.
- Relationship graph browsing.
- Built-in PDF reader work.
- Heavy three-pane management UI.
- Additional special readers unless they directly support deterministic anchor
  capture or jump execution.

Existing code may remain while the anchor model is introduced. Do not remove
working infrastructure casually, and do not reset user changes.
