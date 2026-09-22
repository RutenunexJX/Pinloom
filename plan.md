# Pinloom Plan

Current version: `0.4.4`. Schema version: `16`.

## Current baseline

The resident launcher, canonical semicolon commands, native Anchor confirmation, global File/Clip/Anchor
identities, viewer-independent PDF adapter, annotated-copy PDF highlighting and semantic UI are implemented.
SumatraPDF is the implemented PDF adapter; an adapter boundary does not imply that every viewer is supported.
Current usage and build instructions are in [readme.md](readme.md); product boundaries are in [goal.md](goal.md).

## Completed pending changes — 2026-09-22, source only

- [x] Remove `commandVersionLabel` and its reserved space beside `Navigate`.
  The title-bar version, search and navigation are unchanged.
- [x] Collapse the Command window on loss of application foreground into an
  always-on-top, non-activating three-icon toolbar: PDF Rectangle Anchor,
  PDF Text Anchor and PDF Text Clip. Each action preserves the capture target
  and invokes capture without first expanding the full bar.
- [x] Keep `Shift+Space` to restore the full bar; no bare Shift shortcut and no
  expansion on toolbar hover/focus. Hotkey expansion during floating capture
  is deferred until capture/confirmation returns.
- [x] Hide active file rows without active Anchors, non-empty Tags or Aliases.
  Pinned/explicitly-retained state alone does not make a row visible. Keep
  source files and records needed for Trash, roots and restore.
- [x] Remove synchronous target probing from the rectangle-drag event loop.
  Run DDE in the bounded `pinloom_pdf_probe` helper; timeout/abnormal exit fails
  capture without blocking mouse processing or Escape. Regression tests cover
  blocked probing, cancellation, selection and late worker completion.
- [x] Fix selected-file `删除所有 Anchor` when unchanged historical identities
  conflict. Submit only changed owners to the shared identity registry, retain
  strict validation for changed names/aliases and restore, and compute batch
  claims from the final resource state. Verify grouped files, affected counts,
  immediate refresh, cancellation, reopen and non-deletion of source documents.

Implementation details, test results and remaining live-validation limits are
in [the verification record](docs/PENDING_FIXES_2026-09-22.md). This round does
not publish binaries, replace AppPackage, or change the user's live databases.
The reported PDF hang was not reproduced on the desktop; the WER cross-process
hang evidence does not establish that Nutstore caused it. Advance notice is
still required before any real desktop/mouse reproduction.

## Maintenance and future scope

- Exercise migrations on disposable database copies and verify identity, alias, tag and ranking preservation.
- Keep foreground capture, cancellation and insertion reliable; report failures without modal loops.
- Extend native capture only where a stable interface and a verified locator/jump round trip exist.
  Manual structured locator creation remains an explicit fallback.
- Preserve Obsidian identity, latest-body insertion and race-safe clipboard restoration; richer payloads
  require a concrete use case beyond the existing plain-text contract.
- Keep temporary history bounded; avoid broad content crawling and private application data access.
- Verify clean-profile first run, adapter discovery, runtime dependencies and upgrade behavior on release.
  These are recurring verification requirements, not claims that packaging is still unimplemented.

Do not create a second Anchor model or delete source documents when deleting Pinloom metadata.
