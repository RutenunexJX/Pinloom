# Pending changes verification — 2026-09-22

## Scope and baseline

All six items recorded in `plan.md` were implemented in source under the active
goal. Repository: `E:\Pinloom\Pinloom`, branch `main`, starting and final HEAD /
local `origin/main`: `89998570eeb806944b47aafbbf0a4fd72fa8179f`.
The pre-existing `plan.md` backlog edit was retained and updated. No commit,
push, package deployment, live database write, or other application's source
change was performed in this round.

## Implementation decisions

- Removed the Command input row's version widget and reserved space. The main
  title-bar version and existing command/navigation behavior are retained.
- Added `CommandFloatingController`: application deactivation hides the full
  Command bar and shows three non-activating, always-on-top Capture buttons.
  Theme-aware vector icons, accessible names and tooltips identify the actions.
  Capture remembers the external target before opening dialogs. Hover/focus
  does not expand the bar; Shift+Space restores it. Expansion during floating
  capture is deferred, and stale queued deactivation events are ignored.
- Active Anchor Library rows require an active Anchor, non-empty file Tag or
  Alias. Pinned/explicitly-retained state alone is not a visible marker.
  Existing cleanup rules still protect retained, pinned and root-backed records;
  Trash anchors and source files are not removed by the visibility change.
- Resource repositories submit only changed identity owners to the shared
  registry. Deleting Anchors no longer reclaims unchanged conflicting File or
  Anchor names from legacy data. Changed names, aliases and restores remain
  subject to the existing global uniqueness rule. SQLite computes replacements
  from each resource's final batch state within the transaction, including
  clear/reinsert and repeated-ID batches. No schema or normalization change,
  silent rename, conflict suppression, merge or automatic suffix was added.
- The file context menu still performs a confirmed, atomic soft deletion of all
  active Anchors of the selected grouped file(s). Success refreshes the list and
  reports the affected count; failure now explicitly identifies the operation.

## PDF hang evidence and containment

Windows Application log records 78975–78978 at 22:41:14 and 22:41:42 on
2026-09-22 (Asia/Shanghai) report WER `AppHangXProcB1` and Application Hang for
`pinloom_app.exe`. WER names `Nutstore.WindowsHook.exe` as the other process.
This is a diagnostic lead, not proof that Nutstore caused the hang. No live
hang stack or desktop reproduction was obtained in this round.

The old drag timer synchronously queried DDE on the UI thread. `DdeConnect`
does not accept a timeout; the timeout supplied to `DdeClientTransaction`
does not bound conversation establishment. See the Microsoft documentation
for [DdeConnect](https://learn.microsoft.com/en-us/windows/win32/api/ddeml/nf-ddeml-ddeconnect)
and [DdeClientTransaction](https://learn.microsoft.com/en-us/windows/win32/api/ddeml/nf-ddeml-ddeclienttransaction).

Drag target checks now use one worker at a time, with value-captured session
state and no borrowed overlay pointer. Completion after selection/cancellation
is ignored. Actual DDE runs in the disposable `pinloom_pdf_probe` process, with
a wall-clock limit of `2 * clamp(requestTimeout, 1, 3000) + 500` milliseconds
and up to 1000 milliseconds for terminating a timed-out helper. The reader
and unrelated processes are never terminated. Invalid, failed and oversized
responses are rejected. The final PDF identity and coordinate checks remain.

`pinloom_pdf_probe.exe` is a required sibling of the application executable.
The release script copies it and `--package-check` verifies its health protocol
before settings, instance IPC, user data or native hooks. The health request
does not contact a reader. The release script was not executed this round.

## Verification

Both configurations use Qt 6.10.2 / MinGW 13.1.0 and `CMAKE_BUILD_TYPE=Release`.
Commands were run from the actual repository with the MinGW bin directory on
PATH; CMake and CTest are under `E:\QT6\Tools\CMake_64\bin`.

```powershell
cmake --build build-ela-release --parallel 4
ctest --test-dir build-ela-release --output-on-failure --timeout 60
cmake --build build-classic-release --parallel 4
ctest --test-dir build-classic-release --output-on-failure --timeout 60
git -c core.safecrlf=false diff --check
```

- ELA full build succeeded; final CTest **25/25 passed**, 35.70 seconds.
- CLASSIC full build succeeded; CTest **16/16 passed**, 23.76 seconds.
- ELA coverage includes light/dark at 100%, 125%, 150% and 200%, with generated
  Command and floating-toolbar previews. Light/100% and dark/200% screenshots
  were inspected; a low-contrast system icon was replaced before the final run.
- New widget tests cover all three capture callbacks, non-expansion on focus,
  deferred hotkey expansion, modal protection, actual file-context-menu routing,
  cancellation, grouped resource IDs, affected counts, Trash restore and reopen.
- New identity tests cover unchanged historical File/Clip conflicts, duplicate
  File names and self-duplicate Anchor aliases in disposable legacy SQLite
  fixtures, atomic restore rejection, released-name reuse, final-state batch
  consistency, restart persistence and SQLite integrity checks. Existing global
  identity, concurrency, host, sync and migration suites also passed.
- New PDF tests cover a blocked worker during dragging, prompt Escape handling,
  dialog destruction before completion, ignoring late failure after selection,
  and helper success, malformed response, missing binary, abnormal exit and
  timeout. Existing adapter and presenter tests passed.

An initial core test asserted the former retained-only visibility behavior; it
was updated to the requested rule while retaining the underlying-record
preservation assertion. A new test's repository interface type was corrected
during compilation. Neither remained a final failure. Existing CMake notices
about optional Vulkan headers and Qt private-header version coupling remain.

## Compatibility and remaining validation limits

Schema remains version 16. Existing database files, stable IDs, aliases and
display values are unchanged. No migration or user-data repair runs outside
normal repository initialization; historical conflicts remain visible.

Tests use offscreen windows and disposable data. No desktop mouse automation,
live source-document deletion, real tray test, mixed-monitor DPI certification,
or live reader/hook interaction was performed. In particular, the user's
original drag hang still requires a bounded interactive acceptance check on
the affected PDF reader; the automated results do not identify its exact
external-process cause. Notify the user before any future real mouse operation.

The installed AppSuite/Pinloom package and the running instance are unchanged.
These changes are not available in the formal package until an explicitly
authorized publication/deployment round.

## Authorized 0.4.4 publication follow-up

The user subsequently authorized pushing and replacing the formal package,
and explicitly permitted ending the running Pinloom process for replacement.
The patch version is 0.4.4. Its ELA Release rebuild succeeded and all 25 CTest
targets passed again (39.58 seconds). Schema remains 16. The new PDF probe is
included in the portable runtime. Publication uses a clean source commit,
an independently checked staging package, a recoverable copy of the previous
formal Pinloom directory, and isolated Pinloom updates to suite metadata.
The deployment receipt under `E:\Pinloom\artifacts` records the actual commit,
hashes, backup and replacement result; the preceding sections describe the
implementation round before publication.
