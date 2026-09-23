# Pinloom Plan

Current version: `0.4.5`. Schema version: `16`.

## Current baseline

The resident launcher, canonical semicolon commands, native Anchor confirmation, global File/Clip/Anchor
identities, viewer-independent PDF adapter, annotated-copy PDF highlighting and semantic UI are implemented.
SumatraPDF is the implemented PDF adapter; an adapter boundary does not imply that every viewer is supported.
Current usage and build instructions are in [readme.md](readme.md); product boundaries are in [goal.md](goal.md).

## Floating toolbar follow-up — 2026-09-23, source only

- [x] Replace the opaque rectangular window background with transparent outer
  corners. ELA uses `ElaScrollPageArea` with a 10-pixel radius and the existing
  `ElaToolButton` controls; CLASSIC retains an equivalent rounded fallback.
- [x] Support left-button dragging on the background and all three icons.
  Movement past the system drag threshold suppresses capture; small click
  jitter still behaves as a click. Keep the toolbar within the available screen
  area and remember its moved position across capture and hotkey cycles for
  this running session. Hiding/teardown cancels unfinished gestures safely.
- [x] Preserve the non-activating toolbar and Shift+Space expansion behavior.
  Dragging remains in the controller rather than adding an activating title bar.

Validation: Release builds passed; ELA CTest 25/25 (36.54 s) and CLASSIC 16/16
(23.71 s). The new drag test covers all icons, background, click-vs-drag, right
button, bounds, position retention, hide mid-drag and teardown during a press.
ELA light/dark at 100%, 125%, 150%, 200% asserts transparent corner pixels,
opaque content, Ela component types and theme switching. Light/100% and
dark/200% previews were inspected. Tests were offscreen; no desktop mouse was
used, and mixed-monitor live dragging is not certified. No publication,
AppPackage replacement, live database changes or other application changes
were performed in this follow-up.

### Authorized 0.4.5 publication follow-up

The user subsequently requested a push and replacement of the formal package.
Release 0.4.5 includes the floating-toolbar changes above; schema remains 16.
Both Release builds passed again after the version update. Full ELA CTest
passed 25/25 (41.72 s), and CLASSIC passed 16/16 (31.26 s), using:

```powershell
cmake --build build-ela-release --parallel 4
ctest --test-dir build-ela-release --output-on-failure --timeout 60
cmake --build build-classic-release --parallel 4
ctest --test-dir build-classic-release --output-on-failure --timeout 60
```

Publication uses a clean source commit and an ELA staging package produced by
`packaging/windows/Package-Release.ps1`. Before replacement, verify the staging
runtime with a clean PATH and back up the exact installed Pinloom directory.
Update only Pinloom's version and file hashes plus the shared manifest hash,
holding exclusive handles to the suite metadata and preserving other entries.
The deployment receipt under `E:\Pinloom\artifacts` records the actual commit,
package hashes, backup path and installed-runtime result. No user database or
other application is included in this release change.

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
