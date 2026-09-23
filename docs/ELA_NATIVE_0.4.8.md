# Pinloom 0.4.8: Ela interactions and release verification

## Scope and component responsibilities

Only the ELA build and runtime UI are supported. Qt 6.10.2 EXACT/MinGW 13.1.0
remain required; no database, identity, host, PDF coordinate or capture contract
is changed. Schema remains 16. No other application's source or formal package
is modified by this task.

| Surface | Component / enabled behavior | Pinloom responsibility |
| --- | --- | --- |
| Inputs, buttons, choices | ElaLineEdit, ElaPlainTextEdit, ElaComboBox, ElaPushButton, ElaToolButton, ElaCheckBox, ElaSpinBox, ElaDoubleSpinBox | Validation, original values, Qt edit actions, accessibility, default decisions |
| Library lists | ElaTableView/ListView/TreeView with standard Qt item contracts | Model roles, search, selection, tags, trash and identity writes |
| Scrolling and trees | ElaScrollBar smooth wheel (160 ms); Qt/Ela tree expansion | Precision pixels, Ctrl/Shift, immediate programmatic navigation and new-input cancellation |
| Settings and advanced forms | ElaDrawerArea content snapshot and interruption | Focus return, checked state and values; snapshot capped at 32 MiB and released after settlement |
| Menus and prompts | ElaMenu interruptible 160 ms popup; ElaContentDialog | Qt action semantics, default Cancel/No, exact result mapping; embedded editors stay live |
| Navigation and chrome | ElaNavigationBar, ElaAppBar, ElaToolBar | Action-only routes, existing mapped scrollbars, resident hide-to-tray and close veto |
| Three library layouts | Qt QSplitter | UI-only QSettings size persistence; invalid state falls back, no database writes |
| Specialized surfaces | Existing PDF/capture/locator implementation | Coordinates, external viewer isolation and non-activating floating capture controls |

Ela has no QSplitter replacement. No unrelated tab/dock/calendar/ribbon entry
is added merely to consume a component. Explicit tooltip data and compact notices
retain their existing privacy/quiet-display policy. Local compatibility wrappers
are retained where they encode business contracts, not as supported alternate UI
backends. Shared patches are extensions to pinned Ela, not upstream feature claims.

## Source provenance

Base Pinloom: `369e1b472701d7facf91a01710babde58ffe170c`.
Shared committed source: ZeroSlack `3f1c4afab0d0423c04c3c3af4bb3c3d9aabe5cde`,
menu patch 27 at `75180fad5e5f5142684cf092649deffe5720994d`, plus supplied
patch 28 for ElaListView lifetime and patch 29 for mapped scrollbar origins.
Patch 29 SHA256 is `c292256d9d23cc391b2a185b88d7335f79410ef08e491f727916829c627a88f8`.
Import is incremental, preserving
Pinloom item roles, fonts and specialized application surfaces. No build or
runtime dependency on ZeroSlack's working directory exists.

`thirdparty/elawidgettools/patches/12-pinloom-native-interactions.patch` replays
the vendor delta on the Pinloom base, after existing patches 01–11. Ela MIT,
Font Awesome OFL and the font itself are unchanged. Qt and compiler notices are
taken from the configured SDK, not copied from an old formal package. All local
patches, source provenance and notices are included in the new package.

The follow-up imports shared patch 30 (SHA256
`e69b815ba035831e2a84484acb0c46f957c83f346fff230a8c2b3034d4a44046`) as
`13-pinloom-combo-popup-padding.patch`, based on Pinloom
`51ec1d1429b2f26ca65b848a3176dde9dfb370b8`. Only ElaComboBox.cpp changes in
the library. Fresh popup height includes the vertical layout padding and stays
within screen bounds; repeated visible show settles without applying padding
again. Local lifecycle/input behavior is preserved. Public API/ABI stays p27,
source compatibility patch level becomes 30, and Pinloom stays at 0.4.8.

## Tests

Existing tests are retained. Immediate-only expectations now wait for the actual
animation to settle; dedicated tests also assert that animations start, new input
settles them and actions fire exactly once. Tests cover popup selection, menus and
submenus, disabled/checkable actions, QWidgetAction editors, Escape and focus,
rapid drawer reversals, hide/resize/destruction, snapshot release, scroll direction
changes and precise deltas, inherited text colors, owned focus animations, default
Cancel, masks, parentless prompts, splitter restore and corrupt state.
The mapped-scrollbar regression preserves the previous failure: replacing the
source bar left the overlay visible. It checks source destruction, subsequent
resize/grab, hidden overlay, guarded value callbacks and parent teardown.

Raw focused ElaListView/TreeView/TableView destruction is exercised at 100% and
200%. Existing surface tests cover light/dark at 100%, 125%, 150% and 200%, plus
data/SQLite/global naming, host bridge, captures, PDF helper and package startup.
The full suite has 28 CTest entries. Optional live Sumatra cases remain conditional;
passing offscreen tests is not a live PDF-reader certification.

Patch 30 adds retained regressions for 1/3/5 rows near both screen edges, complete
row visibility, first/last keyboard selection, repeated visible show (including
mid-animation), and close/reopen size stability at 100% and 200%. All six cases
failed before the fix: a 35-pixel row had a 29-pixel viewport, and 3/5-row popups
also lost six pixels. This follow-up reruns the targeted interaction, control,
theme/DPI and widget tests, not the unrelated full suite or performance fixtures.
The prior full-suite/performance evidence remains attached to the prior candidate.

Commands (with the configured Qt/MinGW runtime available):

```text
cmake -S . -B build-ela-0.4.8-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DPINLOOM_UI_BACKEND=ELA
cmake --build build-ela-0.4.8-release --parallel 6
ctest --test-dir build-ela-0.4.8-release --output-on-failure --timeout 90
```

The release receipt records the final clean commit, complete logs and actual
package startup result. The package check uses a minimal Windows PATH and no Qt
SDK plugin path, before any settings, database, IPC, hooks or resident-app logic.

## Same-fixture performance comparison

The same test and fixtures were run against the preserved base executable/DLL
and updated code: 2,000 files (two anchors each), 2,000 saved clips (2,000-character
body), 12 alternating filters, Segoe UI 9, light mode, offscreen DPR 1.
Numbers measure CPU entry dispatch, not compositor/display FPS; the median field
uses the upper middle sample, and P95 is the nearest-rank sample at n=12.
Test load may affect absolute values.

| Window / surface | Median ms before / after | P95 ms before / after | Paint events before / after | Layout events before / after |
| --- | --- | --- | --- | --- |
| 1000×700 / Anchor | 329.29 / 185.49 | 400.64 / 188.01 | 420 / 375 | 124 / 87 |
| 1000×700 / Clip | 12049.79 / 140.39 | 14556.51 / 153.52 | 182 / 183 | 26 / 26 |
| 1600×900 / Anchor | 268.68 / 197.20 | 314.35 / 206.50 | 420 / 375 | 124 / 87 |
| 1600×900 / Clip | 11786.32 / 152.24 | 14124.94 / 162.26 | 158 / 159 | 26 / 26 |

Anchor resize events fall from 50 to 1; Clip remains at 1. Blocking row-by-row
height calculation repeatedly measured table columns; it is replaced with a
compact font-derived row height and batched selection/updates. Selected previews
are explicitly refreshed after the batch. Body text remains preview-only and
searchable. No fixtures, loops, windows or regression cases were removed.
Baseline runs first exceeded 120/300-second test limits; the same fixture then
completed with a longer diagnostic timeout. Both JSON reports are retained in
the delivery evidence; release fixtures complete within the ordinary test limit.

## Release and limits

Package only a clean committed Release build. Reconfigure after commit to record
the exact source identity. The packaging script rejects an old configured commit,
copies the matched Ela DLL, and writes `release-metadata.json` and all-file
`SHA256SUMS.txt` beside the portable directory. No ZIP or old-package backup is
created. AppSuite publication and shared manifests are owned by the coordinator,
not written concurrently by this task.

No desktop mouse, foreground window, real global hotkey, cross-monitor mixed-DPI,
native tray or live-reader interaction was exercised. Offscreen screenshots and
hidden package initialization are not substitutes for that acceptance session.
Qt private-header and imported Qt deprecation warnings remain documented upstream
constraints. The new staged package is not claimed installed until the coordinator
verifies and publishes it.
