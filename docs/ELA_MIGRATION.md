# Ela control migration

Baseline: Pinloom `3dd358d74bc8b0b23f23ca03625a2e4dd610976f` (clean `main`).
No database, locator, identity, clipboard, host, or capture format changes.

## Ownership and dependency

`PINLOOM_UI_BACKEND` selects exactly one compiled backend: `ELA` (default),
`SUITEUI`, or `CLASSIC`. `PINLOOM_UI_STYLE=classic` is an explicit startup-only
fallback. A running application cannot switch renderer; light/dark switching
does not change the renderer. Ela widgets own their control painting; Pinloom
QSS only paints retained views, semantic status labels, and window surfaces.

Ela is imported from the committed ZeroSlack vendor subtree at
`261c90e6d96d9738f7eed2eab27499a25768778d`, not its working tree or build.
The upstream revision is `454cac2d57a47d3cc28577dc817793aec1881ca7`.
See `thirdparty/elawidgettools/UPSTREAM-REVISION.md`, its seven imported patches,
and Pinloom compatibility patches 08–11. Qt 6.10.2 is required because the dependency uses Qt private headers.
No source/build path in ZeroSlack is required to build Pinloom.

Ela's MIT copyright/license and the unmodified Font Awesome Free Solid 6.7.2
font's SIL OFL 1.1 notice are retained. Any binary distribution must include
`thirdparty/elawidgettools/LICENSE`, `Font/FontAwesome-LICENSE.txt`, and source
provenance. The build stages these notices beside the executable, and the
0.4.3 release script copies them into the portable package. Deployment replaces
only `E:\PinloomRoot\AppPackage\AppSuite\Apps\Pinloom` and updates its entries
in the suite metadata. Other AppSuite applications/runtime and user data remain
outside this release's scope.

## Migration inventory

| Surface | Control ownership | Preserved behavior |
| --- | --- | --- |
| Command/main navigation | ElaNavigationBar flyout, tool buttons, search input | All/Anchor/Clip/Inbox/Library/Roots, existing dispatcher routes, keyboard selection, Escape, compact launcher |
| Anchor/Clip/Root Library | Ela filters, search, action buttons, context menus | Models, delegates, scopes, tags, editing, trash, restore, source opening |
| Capture/edit/diagnostics dialogs | ElaAppBar composed with Qt dialogs, Ela inputs/actions | Accept/reject/default/Escape, validation, focus restoration |
| Settings/manual PDF entry | Ela inputs/actions, settings cards and immediate drawers | Settings serialization, collapsed values, native file browsing, locator validation, scrollable advanced fields |
| Text/Clip/capture/diagnostic previews | Ela plain-text editors and scrollbars | Complete text, read-only/copy, wrapping, technical font, target-line highlighting, Qt text accessibility |
| Settings and Anchor Inspector scrolling | Ela scroll areas and scrollbars | Resizable content, ensure-visible, keyboard and wheel modifiers, fixed settings action footer |
| Resident main-window menu/status bars | Ela bars with existing QActions and status API | Keyboard menu dispatch, launcher hide/show, diagnostics, hide-to-tray, large-font sizing |
| Tray | Ela menu with original QActions | Native tray activation, pause/resume, hide-to-tray, quit |
| Alias/tag/name prompts | Ela chrome, inputs, text labels and actions | Explicit acceptance, cancel leaves data unchanged |
| PDF canvas/region overlay/locator image previews | Original specialized views and image scroll container; semantic palette/QSS | Coordinate mapping, native capture, adapter, preview and selection |
| Library tables / command results | ElaTableView / ElaListView with independent QStandardItemModels | Stable IDs, content privacy, sorting, editing, multiline rows, keyboard and accessibility |
| File directory tree | ElaTreeView with the existing filesystem model | Expansion, selection, sorting, metadata actions |
| Tag multiselect popups | ElaListView-backed model and shared Ela-painted popup frame | Filtering, checkbox keyboard input, creation, tag colors, deferred save, screen bounds and dismissal |
| Tooltips | ElaToolTip painting with Qt help-event control | Explicit widget/ToolTipRole text, dynamic updates, literal wrapping, owner lifetime, no Clip body fallback |
| Static titles / descriptions | ElaText using Qt label text/selection semantics | Semantic colors, font roles, wrapping, form buddies |
| Windows and ordinary notices | ElaAppBar composition / bounded ElaMessageBar | Qt close/reject/hide-to-tray; persistent, copyable status remains authoritative |
| QFileDialog/QMessageBox | Original platform/file and safety-confirmation UI | OS navigation, conservative defaults, existing tests and semantics |

Common controls are constructed through `PinloomUiControls`; their Qt base
contracts, object names and existing signal connections remain intact. Dialog
button mapping is explicit because Qt cannot inject a subclass into its private
standard-button factory. Popup selection/dismissal uses Qt's immediate lifecycle
with Ela's painting, avoiding the upstream non-interruptible combo animation.

The earlier four-family follow-up migrated previews, scrolling and resident bars.
The final six-family round also replaces the item views, tag-picker view, static
text, ordinary dialog chrome, window chrome, navigation and ordinary notices.
New text editors retain the QPlainTextEdit API, standard localized
context actions and caller-selected Tab behavior. Their menu action container
is lifetime-paired without inheriting ElaMenuStyle (which expects ElaMenu).
Scrollbars use Qt wheel/context behavior and no decorative hover/range motion;
the scroll areas restore AsNeeded policies rather than Ela's AlwaysOff default.
Menu/status rows respond to font metrics; changing theme does not resize them.

`PinloomItemViews` owns standard models and forwards the small convenience API
used by existing controllers. This is a model/view replacement, not inheritance
from QTableWidget/QListWidget. Ela's custom item renderer opts into Qt role,
checkbox, selection, text-elision and multiline-size contracts; compact row
spacing and existing tag-chip delegates remain. Clip content is not stored in
left-side items, tooltips or accessible names. Theme finalization reapplies view
palettes after QSS polishing, including the Trash selection color.

Window chrome uses composition rather than ElaWindow's private stacked-page
container. All window close requests pass through existing Qt close/reject logic;
the resident window still hides to tray. Command-window sizing includes title-bar
margins on both hotkey presentation and content updates. No business pages are
reparented into the navigation control: its six action destinations invoke the
existing commands and do not create Ela global route-history entries.

Notifications are supplemental: one immediate, non-focus-stealing ElaMessageBar
per visible surface, deduplicated and replaced safely, with a 4.5-second expiry.
The bar shows at most three lines; its tooltip/accessibility description and the
persistent status retain full text. Search/count refreshes do not create notices.
Startup failures, destructive confirmations, native file selection and specialized
PDF windows remain unchanged. Classic/SuiteUi retain native chrome and persistent
status. In an Ela build's runtime Classic mode, the compiled list/table subclass
still inherits Ela but detaches its renderer and uses native Qt style/scrollbars;
the non-Ela builds have no Ela link dependency.

The final polish round adds `PinloomUiSurfaces`. Its tooltip controller only
handles explicitly Ela-managed controls and their children, stopping at a window
boundary so native safety/file dialogs remain native. The real ElaToolTip is
constructed without Ela's hover filter, then parent-owned. Qt help events keep
their delay; the adapter preserves focus, expires the tip, updates changing
widget/model text, and dismisses on row movement, scrolling, key input, hiding
or destruction. Text is literal, wrapped to screen bounds, and limited to twelve
visible lines; the original tooltip and accessible description retain full text.
Only explicit ToolTipRole is read for table/list cells, never DisplayRole or
arbitrary user data. No left-side Clip body or snippet is introduced.

Settings have three ElaScrollPageArea groups and an ElaDrawerArea for advanced
clipboard limits and filters. Core privacy switches and the summary stay visible.
The advanced locator form uses the same checkable, keyboard-accessible header;
its dialog now has a scrollable form and fixed action footer so the final Zoom
field remains reachable on short windows. Collapse preserves values and returns
focus from hidden content. Opt-in vendor patch 11 makes drawer transitions
immediate without affecting upstream defaults. Shared tag popup frames keep
Qt::Popup lifecycle, use Ela border/shadow colors, clamp to the screen, and
retain native frames in alternative backends.

## Verification

Verified on 2026-09-22 with Qt 6.10.2, MinGW 13.1 and Ninja, using separate
`build-ela-release`, `build-classic-release` and `build-suiteui-release` directories.
No interactive application instance or desktop mouse automation was used.

```text
cmake --build build-ela-release --parallel 4
ctest --test-dir build-ela-release --output-on-failure
```

Release full build succeeded; all **23 CTest targets passed**, including the
existing core, capture, PDF adapter/presenter, SQLite, Clip, global identity,
widget, command, host and SuiteApp regressions. The nine Ela targets include
the renderer contract plus light/dark at 100/125/150/200%.

Both alternative compiled backends built `pinloom_app` and passed the three
`widget_smoke|visual_theme|suiteui_controls` tests. The same three tests passed
with `PINLOOM_UI_STYLE=classic` in the Ela build. Contradictory
`PINLOOM_UI_BACKEND=ELA` + legacy `PINLOOM_ENABLE_SUITEUI=ON` is rejected at
configuration. The initial migration only parsed the packaging PowerShell file.
Release 0.4.3 additionally adds the `pinloom_package_smoke_test` CTest target and
`pinloom_app.exe --package-check`: UI rendering plus in-memory SQLite, returning
before settings, single-instance IPC, user databases or native hooks. The final
release validation runs the full **24-target** CTest suite and checks the actual
deployed package with SDK paths excluded.

The renderer test checks actual Ela types, Qt accessibility and keyboard input,
dialog standard-button identity and signals, Enter/Escape, name/int prompts,
combo selection, theme switching with a visible popup, immediate parent teardown,
unchanged capture-dialog geometry, full selected Clip content, native tray action
routing without showing an icon, and hide-to-tray. Settings have a scrollable
form and fixed action footer; status-label height and numeric editor width have
explicit no-clipping assertions. Existing tests were retained; their button-box
lookups now use the adapter's standard-button mapping.

Follow-up contracts additionally cover Unicode/full-text copy, read-only edit
and paste rejection, editable undo/redo, localized context actions, focus/Tab,
wrap/no-wrap, text-file target-line highlights, keyboard/modified-wheel scrolling,
ensure-visible at narrow widths, menu keyboard activation and overflow, accessible names,
status messages, large fonts, launcher visibility, and editor/menu/bar teardown
with a visible context popup. Qt deferred menu/layout events are drained before
focus and geometry assertions. The original SuiteUi base-style assertion captures
the pre-theme style at test-suite initialization, so new tests do not change its
baseline through a previously installed stylesheet proxy.

128 fixture PNGs are generated under `build-ela-release/ui-preview/<theme>-<scale>/`:
`command`, `controls`, `clips`, `anchors`, `settings`, `settings-clipboard`,
`anchor-capture`, `clip-capture`, `extended-controls`, `navigation`, `tag-picker`,
`roots`, `notification`, `tooltip`, `settings-advanced`, and `manual-advanced`.
The Windows offscreen font loader uses the
local Windows font directory; renderer fixtures explicitly select Segoe UI.
No system font is copied or redistributed. An initial fontless offscreen run
was discarded for visual acceptance. Final light/dark and 200% images were
inspected after adding font resolution and no-clipping checks.

Final-round contracts add standard-model/role identity, editing, sorting, tag
checkboxes, filtering and multiline rows; tree expansion and keyboard selection;
form-label buddies; title-bar close/reject/hide-to-tray; complete persistent status,
notice replacement and resize; six keyboard navigation destinations, glyph coverage,
Escape and repeated visible-popup parent teardown. Tag-text contrast has a
pixel-level assertion across both themes and every scale. Native-window close
events also test the resident hide-to-tray path. Initial issues found during
verification (title-bar height, palette order, missing icon glyph, row spacing
and dark-theme tag-text contrast)
were corrected before the final run. No existing test assertion was removed;
host-height assertions now include chrome and additionally check content bounds.

Polish contracts cover dynamic/cleared/model tooltips, full-text accessibility,
literal rendering, screen-edge bounds, expiry, focus, native safety exclusion,
Clip preview-only privacy, immediate drawer toggles/value retention, section
ownership/bounds, advanced-field reachability, and popup keyboard/teardown.
Visual inspection found and corrected two integration issues: unparented nested
settings layouts causing overlap, and the unscrollable expanded manual form
clipping its last field. Dedicated ownership and geometry assertions now cover
both, in addition to the existing behavior tests.

The imported dependency differs from the committed comparison source only in
the twenty-eight files recorded by patches 08–11, those patches, and the provenance
document. All four patches pass `git apply --no-index --check` against the imported
reference (they touch disjoint files). Ela and font license/font bytes are unchanged.
Staged whitespace checks exclude the recorded `.patch` files: unified-diff blank
context lines intentionally contain a space, and imported whitespace/BOM history
must not be rewritten. Source files pass the check; patches are checked by applying
them to the pinned reference instead.

## Changed first-party files

- Build/documentation: `CMakeLists.txt`, `readme.md`, this inventory, and
  `packaging/windows/Package-Release.ps1` (DLL/notice distribution).
- Shared control boundary: `include/pinloom/widgets/PinloomUiControls.h`,
  `src/widgets/PinloomUiControls.cpp`, `src/widgets/PinloomUiSurfaces.cpp`, `PinloomItemViews.h/.cpp`,
  `src/widgets/PinloomVisualTheme.cpp`.
- Dialogs: headers/sources for `AnchorCaptureDialog`, `ClipCaptureDialog`,
  `ManualPdfAnchorDialog`, `PinloomSettingsDialog`, and `TextPreviewDialog`.
- Surfaces: headers/sources for `AnchorLibraryWindow`, `ClipLibraryWindow`,
  `LibraryRootWindow`, `PinloomCommandPanel`, `PinloomMainWindow`, `PinloomPanel`;
  `ClipTrayPresenter.cpp`, `AnchorLocatorPreviewWidget.cpp`, and the existing UI
  prompt sites in `src/app/main.cpp`.
- Tests: `ela_controls_test.cpp`, `suiteui_controls_test.cpp`,
  `visual_theme_test.cpp`, `widget_smoke_test.cpp`.
- Dependency: `thirdparty/elawidgettools/` (428 files including licenses,
  source provenance and compatibility patches).

## Validation limits

Live SumatraPDF capture/DDE, real global-hotkey registration, Win32 title-bar
drag/resize and mixed-monitor DPI,
and native tray interaction require a separately controlled desktop session;
offscreen tests do not certify them. No database/schema/identity service changes
or changes to another application/runtime are part of this migration.
The Qt private-header dependency requires rebuilding/revalidating Ela
when upgrading Qt; imported deprecated-Qt-API compiler warnings remain upstream.

## Delivery state

The four Ela migration rounds are published together as release 0.4.3 on `main`,
following explicit user authorization to commit, push and package. The release
commit is based on `3dd358d74bc8b0b23f23ca03625a2e4dd610976f`; the package README
records its exact abbreviated source revision and requires a clean worktree.
`E:\Pinloom\artifacts\Pinloom` is the staging artifact. The formal package target
is `E:\PinloomRoot\AppPackage\AppSuite\Apps\Pinloom`; the prior package and suite
metadata are backed up before replacement. Pinloom's manifest version and file
checksums are updated without changing other components. There are no database
migrations or compatibility changes to stored data.

Final validation logs are local build artifacts:
`build-ela-release/release-0.4.3-validation.log` (24 tests),
`build-ela-release/ela-polish-validation.log`,
`build-classic-release/ela-polish-validation.log`,
`build-suiteui-release/ela-polish-validation.log`, and
`build-ela-release/classic-fallback-polish.log`.
The two existing optional live Sumatra probes remain skipped when no live target
is configured; they are not silently counted as exercised desktop integration.
