# Pinloom Plan

Current version: `0.4.7`. Schema version: `16`.

## Current baseline

The resident launcher, canonical semicolon commands, native Anchor confirmation, global File/Clip/Anchor
identities, viewer-independent PDF adapter, annotated-copy PDF highlighting and semantic UI are implemented.
SumatraPDF is the implemented PDF adapter; an adapter boundary does not imply that every viewer is supported.
Current usage and build instructions are in [readme.md](readme.md); product boundaries are in [goal.md](goal.md).

## 方向右键菜单与搜索结果布局

已完成本地实现与验证，正式包待统筹发布。

- [x] 将方向右键（→）打开的菜单简化为“图标 + 简短名称”，整理当前混乱的布局。
- [x] 将该菜单的第一项改为“打开所在文件夹”（Open folder）；无本地文件、目录缺失或已删除条目保留不可用原因。
- [x] 搜索结果改为图标、名称、简短位置和类型标签组成的两行列表；结果数量与键盘提示合并到单行底栏，按结果数量调整高度并保留滚动。

Release 构建及 12/12 个相关 CTest 组通过；两个新增回归用例在 200% 缩放下另行通过。
已核对明暗主题的 100%/200% 截图。证据位于 `build/validation/command-layout-20260927`。
本轮验证使用 offscreen 窗口和临时数据，不包含真实桌面手工操作或正式包部署。

## Anchorless file metadata clearing — source only

- [x] Add `清除全部 Tag 和 Alias` to the Ela file context menu when all selected
  files have no active anchors. Do not offer it in Trash or for mixed selections
  containing active anchors. Confirm before clearing; keep source files, file
  records, titles, Pinned/retained state and anchors in Trash unchanged.
- [x] Revalidate the records in the management service after confirmation and
  submit one atomic repository batch, with the existing identity validation and
  undo history. Retain all underlying resource IDs for grouped file rows,
  including records without anchors. On success discard only those rows'
  pending inline edits and hide the now-unmarked rows using the existing policy.
- [x] Test InMemory/SQLite parity, batch rejection, undo, alias reuse,
  persistence/integrity, context-menu activation, cancellation, grouped records,
  pending-edit cleanup, mixed/Trash selection and a new anchor arriving during
  confirmation. Source files are checked for unchanged contents.

Both Release builds passed. Full ELA CTest passed 25/25 (41.66 s); CLASSIC
passed 16/16 (25.94 s), using `cmake --build build-ela-release --parallel 4`
and `ctest --test-dir build-ela-release -C Release --output-on-failure --timeout 60`
with the corresponding `build-classic-release` commands. `git diff --check`
passed. Tests used Qt offscreen; no desktop mouse, live database, AppPackage,
version or schema changes were made. At the source-only handoff this follow-up
was not committed or pushed.

### Authorized 0.4.7 publication follow-up

The user subsequently requested a push and replacement of the formal package.
Release 0.4.7 includes the anchorless file metadata clearing above; schema
remains 16. Both Release builds passed after the version update. Full ELA
CTest passed 25/25 (47.75 s), and CLASSIC passed 16/16 (30.74 s), using the
commands above. `git diff --check` passed. Tests used Qt offscreen and did not
interact with the desktop or user databases.

Publish a clean source commit to the existing origin/main and package the ELA
Release build with `packaging/windows/Package-Release.ps1`. Verify the staged
runtime using a clean PATH, preserve the installed runtime license notices and
back up the exact formal Pinloom directory before replacement. Replace only
while no resident Pinloom process is running. Hold exclusive handles to the
suite manifest and checksum index, recheck their current baseline and update
only Pinloom's version/hashes plus the shared manifest timestamp/hash. Other
application/runtime files and metadata stay intact. The deployment receipt
under `E:\Pinloom\artifacts` records the actual commit, hashes, backup and
installed-runtime results. No schema or user database changes are required.

## Anchor Library Ela rewrite — 2026-09-23, source only

- [x] Recompose Anchor Library as an `ElaScrollPage`, three `ElaScrollPageArea`
  cards, an `ElaDrawerArea` filter panel, `ElaToolBar` and `ElaStatusBar`.
  Tables, inputs, menus, labels and alias editors use the real Ela controls.
  Keep the existing ElaAppBar window integration and the CLASSIC fallback.
- [x] Default to compact, stretching table columns. Details exposes the existing
  path, time and usage fields; it does not remove or change stored metadata.
  Keep tag chips on one line, expose full tags in tooltips, and bound filter
  widths even with long tag names. Preserve active filters in the closed drawer.
- [x] Show title, source path and Open/Enlarge actions beside the preview.
  Use Ela controls in the enlarged view. Successful automatic previews,
  including their asynchronous loading state, update status without a toast.
- [x] Replace native destructive confirmations with `ElaContentDialog`.
  Cancel is the default; Escape/Enter cancel unless Confirm is explicitly
  chosen. Bind close callbacks to QObject lifetime and dismiss the parent mask.
- [x] Retain search, saved views, Trash/restore, inline edits, tag scopes,
  context menus, batch management and source opening. Add Ctrl+F, F5 and row
  Enter while preserving existing save/delete/multi-selection shortcuts.
  Verify accessible names, keyboard focus, supported icon glyphs and tag contrast.

No data repository implementation, identity rule, schema, live database, AppPackage, other
application or version was changed. Qt models/layouts/splitters, the custom
locator painter and native OS file pickers remain supporting infrastructure;
there is no substitute styled control where an applicable Ela control exists.

Validation commands (Release, Qt offscreen; no desktop mouse interaction):

```powershell
cmake --build build-ela-release --parallel 4
ctest --test-dir build-ela-release -C Release --output-on-failure --timeout 60
cmake --build build-classic-release --parallel 4
ctest --test-dir build-classic-release -C Release --output-on-failure --timeout 60
```

Both Release builds passed. Final ELA CTest passed 25/25 (41.09 s), and
CLASSIC passed 16/16 (25.45 s). `git diff --check` passed. Light/100% and
dark/200% compact, expanded-filter and empty-state screenshots were inspected.

The regression suite covers actual Ela types, filter collapse/retention,
920-pixel compact width with long paths/tags, Details, empty states, inline Ela
editors, default-cancel confirmations, mask cleanup, keyboard/accessibility,
expanded previews and quiet asynchronous PDF cache previews. The Ela matrix
also exercises light/dark at 100%, 125%, 150% and 200%. Live desktop behavior
is not certified by these offscreen tests. At the source-only handoff this
rewrite was not committed, pushed or included in the installed 0.4.5 package.

### Authorized 0.4.6 publication follow-up

The user subsequently requested a push and replacement of the formal package,
and authorized stopping only Pinloom and its capture helper before replacement.
Release 0.4.6 includes the Anchor Library Ela rewrite above; schema remains 16.
Both Release builds passed again after the version update. Full ELA CTest
passed 25/25 (41.65 s), and CLASSIC passed 16/16 (25.30 s), using the commands
above. The build retains Ela's existing Qt 6.10.2 private-header version binding.
Publish a clean source commit to the existing origin/main and package the ELA
Release build with `packaging/windows/Package-Release.ps1`. Verify the staged
runtime using a clean PATH before changing the installed package.

Back up the exact installed Pinloom directory before replacement. Hold exclusive
handles to the suite manifest and checksum index, recheck the live baseline,
and update only Pinloom's version/hashes plus the shared manifest hash. Preserve
other component entries and runtime license notices. The deployment receipt
under `E:\Pinloom\artifacts` records the actual commit, hashes, backup and
installed-runtime results; no user database or other application is deployed.

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
