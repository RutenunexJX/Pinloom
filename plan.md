# Pinloom Plan

This plan tracks work that remains after the launcher, SumatraPDF, Inbox, and
Saved Clip foundations are in place. Product boundaries are defined in
`goal.md`; current usage and build instructions are in `readme.md`.

## Current Baseline

- One resident `pinloom_app.exe` process owns the tray, `Ctrl+Space` Command
  Window, contextual `Hyper+S` chord, anchor repository, and Clip repository.
- The Command Window searches and acts on Anchors, Saved Clips, Inbox files,
  and local resources through one entry/action protocol.
- Anchors use one canonical model: target application, target file or URI,
  locator type, and locator JSON. SQLite schema version 11 performs a one-time
  migration from older anchor columns without retaining a second runtime
  model.
- SumatraPDF is the only PDF host. DDE supplies the active file, page, zoom,
  and same-page rectangle coordinates; launcher execution restores page and
  position and adds a temporary Pinloom highlight.
- Excel, Word, PowerPoint, and Visio have structured jump executors for existing
  anchors. Unsupported or malformed locators fail explicitly.
- Saved Clips support temporary clipboard history, named persistence, search,
  insertion, Obsidian-backed Markdown storage, and contextual selected-text
  capture.
- Inbox captures local file objects in Link mode and opens them through the
  operating-system default application.
- Directory crawling, library-root management, relationship graphs, standalone
  Clip picker UI, JSON Clip archive, and dormant anchor archive/health shells
  are not part of the runtime.

## Phase 1: Daily-Use Hardening

- Make command, capture, insertion, and jump failures diagnosable without
  modal error loops.
- Preserve foreground-window and insertion-target behavior across repeated
  Command Window use.
- Exercise schema migration against a copy of an existing personal database
  before packaging.
- Keep search and action latency stable as anchor and Clip counts grow.

Exit criteria:

- Existing schema versions upgrade to version 11 without losing resources,
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

- Improve UI Automation selection coverage without synthesizing `Ctrl+C`.
- Preserve Obsidian note identity across edits and renames.
- Keep clipboard restoration race-safe and observable through diagnostics.
- Add richer payload kinds only after the plain-text contract remains stable.

Exit criteria:

- `Hyper+S` consistently distinguishes accessible selection from caret-only or
  unknown state.
- Saved Clip insertion reads the latest external Markdown body.
- Temporary history remains bounded by policy and excluded applications.

## Phase 4: Distribution

- Produce a repeatable Windows package containing the required Qt runtime and
  PDF proxy executable.
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
