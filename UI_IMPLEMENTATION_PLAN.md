# Pinloom UI and Native Capture Implementation

Status: active

Date: 2026-08-25

This document is the executable UI/capture slice of the active change set in
`plan.md`. It does not change Pinloom's deterministic Anchor product boundary.

## Command surface

- Preserve the foreground window, focused control, and native document target
  before the resident `Shift+Space` window becomes active.
- Remove whitespace legacy command parsing and documentation. Semicolon forms
  are canonical; colon forms remain read-only transition compatibility.
- Keep the input first in focus order. Add a compact persistent quick-action row
  directly below it with `Rectangle Anchor` and `Text Anchor`.
- Render search results as compact two-line rows: primary name/action, secondary
  target/provenance, type badge, and optional keyboard hint. Selection, hover,
  disabled reason, and destructive actions use semantic tokens.
- Use a compact status footer for current mode and failures. Empty results show
  the recognized grammar and one relevant corrective action.
- Remove bitmap texture dependence from information-bearing surfaces; contextual
  Anchor/Clip/Inbox identity is represented by accent tokens and badges.

## Shared confirmation

- Introduce one `AnchorCaptureDraft` independent of widgets. It carries target
  app/file, locator type/JSON, suggested name, aliases, tags, pinned state,
  provenance, mutation requirement, and validation diagnostics.
- Every native capture returns a draft. No capture path writes the repository.
- One confirmation dialog presents target and locator summary plus editable
  name, aliases, tags, and pinned state. A mutation authorization section is
  shown only when a Word bookmark, Visio UniqueID, or Excel defined name must be
  created in the user document.
- Cancel and validation failure create no Anchor and perform no document
  mutation. Mutation and repository save are coordinated so failure is
  diagnosable and no misleading weak locator is substituted.

## PDF capture, jump, and highlight

- Rectangle capture reactivates the remembered SumatraPDF window before showing
  the capture overlay and records file, page, zoom, scroll context, and PDF
  coordinates.
- Text capture uses the remembered SumatraPDF document and selected text. The
  locator stores page hint, occurrence/context, and rectangle fallback when
  available; repeated text never resolves by first match without verification.
- Jump completion uses bounded DDE/state polling for expected document, page,
  zoom, and visible target. Timeouts report the observed state and retry route.
- A `PdfHighlightManager` owns click-through document-scoped highlights keyed by
  window/document/Anchor. Multiple highlights coexist and remain until the
  target document/window closes.
- Highlight geometry is recalculated on move, resize, page, zoom, and scroll.
  Wrong-page/offscreen highlights hide and reappear when visible.

## Word, Visio, and Excel

- Capture dispatch is selected from the remembered process/window; unsupported
  applications fail explicitly.
- Word captures/reuses a bookmark for the selection/caret. Creating a
  Pinloom-owned bookmark requires explicit mutation authorization.
- Visio requires exactly one selected shape and records document, page `NameU`,
  and persistent shape UniqueID. Creating the ID requires authorization.
- Excel records workbook, worksheet, and exact absolute range. It prefers an
  exact existing defined name; creating a Pinloom-owned name is optional and
  requires authorization.
- Unsaved, protected, read-only, missing, empty-selection, and ambiguous cases
  produce precise failures and no generic fallback Anchor.

## Application UI pass

- Define light/dark semantic tokens for shell, panel, raised surface, input,
  row, badge, focus, status, and product accents.
- Apply the system to Command, Anchor Library, Clip Library, Root Library,
  Settings, capture confirmation, preview, and diagnostics windows.
- Use 4/8/12/16/24 px spacing, 32 px default controls, visible keyboard focus,
  aligned primary actions, non-modal routine status, and explicit empty states.
- Anchor lists prioritize name, locator type, target, tags, pin/freshness, and
  jump action. Advanced metadata remains available without occupying the first
  visual level.

## Verification

- Parser tests prove all removed whitespace forms are ordinary queries and all
  canonical semicolon forms retain behavior.
- Widget tests cover quick actions, target snapshot preservation, shared
  confirmation, cancellation, validation, focus order, compact/expanded height,
  light/dark tokens, and 960/1440 px layout.
- Deterministic provider tests cover PDF state polling and highlight lifecycle.
- Native adapter tests use injectable backends; optional real-app tests use only
  disposable `.pdf`, `.docx`, `.vsdx`, and `.xlsx` fixtures.
- All configured core, SQLite, clip, capture, host, suite, and widget tests pass.
- Changes are committed and pushed independently; packaging is not run.
