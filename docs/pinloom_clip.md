# Pinloom Clip

Pinloom Clip is a local companion app for searchable insertion and clipboard
recall inside the Pinloom ecosystem. It is intentionally separate from the v1
anchor launcher work so the main product remains focused on deterministic
native-app anchors.

## Product Boundary

Pinloom Clip listens to system clipboard changes and captures a local clip
history. The MVP only promises plain text. Rich text, images, files, and perfect
format preservation are outside this first slice.

Temporary Clip History is private working memory. It is not added to the
Pinloom main search index by default. A clip becomes a Pinloom-managed object
only when the user explicitly saves it.

Saved Clips can be represented as Pinloom anchors with:

```text
target_app = pinloom.clip
locator_type = clip.insert
target_uri = clip://<clip_id>
locator_json = {"clip_id":"<clip_id>","mode":"paste"}
```

The locator means "insert this saved clip", not "search all clipboard history".

## MVP Model

The current skeleton models text clips with:

- clip id
- kind: text
- text, preview, content hash, and byte size
- temporary or saved state
- name, aliases, tags, and pinned state
- created_at, updated_at, used_at, and expires_at timestamps
- optional source_app

## SQLite Persistence

The Clip companion now has a SQLite repository skeleton for plain-text clips.
It stores the same metadata as the in-memory model in a dedicated `clips` table:
id, kind, temporary/saved state, text, preview, content hash, saved name,
aliases, tags, pinned state, timestamps, source app, and byte size.

The first persistent slice is intentionally text-only. It does not create a
blob directory, does not store images, and does not attempt rich text, HTML, or
RTF fidelity yet. Future clipboard listeners should still pass through the same
capture policy checks before writing rows.

## Qt Text Clipboard Capture

Pinloom Clip now has a lightweight Qt clipboard capture service for the MVP
text loop. It listens to `QClipboard` text changes through a small injectable
clipboard source, so the real app can use `QGuiApplication::clipboard()` while
tests can drive the same capture path without touching the user's clipboard.

The service supports start/stop, pause/resume capture, source-app metadata,
one-shot suppression for future self-written clipboard changes, last captured
id/status/error state, and a captured signal. It only captures plain text and
delegates blank, size, duplicate, pause, and source-app exclusion decisions to
the existing Clip repository policy layer.

This capture service alone is still not the tray app, floating picker, rich
text capture, image capture, or blob storage.

## Text Clip Insertion

Pinloom Clip now has a small text insertion service for closing the MVP loop
from a selected clip id to a paste attempt. The service can load a clip from the
in-memory or SQLite repository, remember the current clipboard text, write the
target clip text, call an injected paste invoker, and optionally restore the
original clipboard text after success.

The insertion path is intentionally dependency-injected: tests use fake
clipboard accessors and fake paste invokers, and the core service does not need
to touch the real system clipboard or an active window. It also accepts a
suppression callback, so callers can wire it to
`ClipboardCaptureService::suppressNextChange()` before self-written clipboard
changes and avoid recapturing the text it just staged for paste.

The current insertion service reports last inserted id, last status, and last
error for missing clips, empty or non-text clips, clipboard unavailability,
clipboard write failure, paste failure, and restore failure.

The core now also provides a default platform paste invoker. On Windows it uses
an injectable key sender backed by Win32 `SendInput` to emit Ctrl down, V
down/up, and Ctrl up after the insertion service stages the target text on the
clipboard. Tests inject a fake key sender, so the test suite verifies the exact
sequence without sending real keys to the active window. On non-Windows
platforms the default sender is an unavailable fallback and paste attempts fail
safely as `PasteFailed`.

Window focus, activation timing, retry/backoff, and any delay between writing
the clipboard and sending Ctrl+V remain app-layer/runtime responsibilities. The
platform invoker deliberately does not bypass the insertion service's
suppression or optional clipboard restore logic.

This insertion service alone is still not the tray app, rich text insertion,
image insertion, HTML/RTF/blob handling, or a full window-targeting paste
workflow.

## Saved Clip Search And Picker Model

Pinloom Clip now has a core saved-clip search model for the future
Listary-style picker. The model is UI-free and works against both the
in-memory and SQLite clip repositories. A query returns compact picker results
with clip id, display name, preview, matched field/value, score, rank, saved or
temporary state, aliases, tags, pinned state, and created/updated/used
timestamps.

The conservative default search scope is saved clips only. Temporary Clipboard
History remains private working memory and is excluded from the default picker
search unless the caller explicitly enables `includeTemporary`. Empty queries
return pinned and recently used saved clips so the picker can open to useful
defaults without exposing temporary history.

Search is case-insensitive across saved clip name, aliases, tags, preview, and
text. A `#tag` query is treated as tag-first search. Ranking is deterministic:
exact saved-name matches rank ahead of alias matches, then tag matches, then
preview/text prefix or contains matches. Pinned clips and recent usage break
ties before updated/created timestamps and clip id.

## Clip Picker UI Skeleton

Pinloom Clip now has a lightweight Qt `ClipPickerPanel` skeleton for the
Listary-style picker surface. It is intentionally compact and keyboard-first:
a search box, a result list, and a status line. The panel uses
`ClipSearchService`, keeps the conservative saved-only default scope, refreshes
results as the query changes, and exposes row data for display name, preview,
matched field/value, tags, aliases, pinned state, rank, and score.

The picker activates the selected result by passing its clip id to an injected
insertion handler. A small helper can adapt `ClipInsertionService` into that
handler, but widget tests use fakes and do not touch the system clipboard or
send real paste keys. Successful activation updates the picker status and can
close the widget; failures surface the error and leave the clip unchanged.

This widget alone is still not Windows foreground-window orchestration, rich
text insertion, image insertion, HTML/RTF/blob handling, or a full
window-targeting paste workflow.

## Clip Hotkey Service Skeleton

Pinloom Clip has a small global hotkey service that remains available for
standalone picker hosts and unit tests. The core models a hotkey configuration
with a key and Qt keyboard modifiers, defaults to `Ctrl+Shift+V`, and can
expose a display string for settings or status surfaces. The main
`pinloom_app.exe` path does not register this Clip hotkey; Clip is reached from
the unified `Ctrl+Space` launcher with the `c` command prefix.

The service is dependency-injected around a backend interface. Tests use a fake
backend, so they can verify registration state, activation signals, failure
errors, and duplicate start/stop behavior without registering a real global
hotkey. The default platform backend is intentionally thin: on Windows it is
structured around Win32 `RegisterHotKey`/`UnregisterHotKey` plus a Qt native
event filter for `WM_HOTKEY`; on non-Windows platforms it reports unavailable.

A tiny picker hotkey adapter can adapt the service activation signal into an
injected show/focus handler. It does not own a tray app, does not auto-start
with the system, does not coordinate foreground windows, and does not decide
paste timing. Those remain app-layer responsibilities for the later
foreground-window and paste-polish workflow.

## Tray Presenter And Picker Host Skeleton

Pinloom Clip now has a lightweight tray/picker host controller skeleton for
the future resident app. `ClipTrayController` is a QObject-based runtime layer
that starts and stops the injected `ClipHotkeyService`, routes both hotkey
activation and manual show requests through the same injected show/focus picker
handler, and tracks status, last error, running state, capture pause state, and
picker show count for tests and future status surfaces.

The controller intentionally exposes only a simple tray action model: show
clipboard, pause/resume capture, and quit. Pause/resume is handler-injected so the
future app can connect it to `ClipboardCaptureService`, while tests can verify
state changes without touching the user's clipboard. Quit is exposed as a
signal only.

The Qt Widgets layer now has a small `ClipTrayPresenter` and tray backend
interface. The presenter maps controller status into the tray tooltip and maps
the action model into backend menu actions. Widget tests use a fake tray backend
to cover show clipboard, pause/resume labels and checked state, quit routing,
start/stop status synchronization, and hotkey failure text without creating a
real `QSystemTrayIcon` or touching the system tray. A thin
`QtSystemTrayIconBackend` exists as the real host seam for the later resident
entry point.

This is still not a complete resident tray app, not auto-start registration,
not Windows foreground-window orchestration, and not paste timing, focus
recovery, or rich content handling. Those remain app-layer work for the later
resident tray workflow.

## Resident Runtime Composition Skeleton

Pinloom Clip now has a small widgets-layer `ClipResidentRuntime` composition
skeleton. It wires an injected repository together with clipboard capture,
saved-clip search, text insertion, the picker panel, hotkey service, tray
controller, tray presenter, and tray backend. The runtime starts and stops
capture, hotkey registration, and tray presentation as one unit; routes hotkey
and tray show actions through the same picker show/focus path; adapts picker
activation into `ClipInsertionService`; connects insertion self-writes to
`ClipboardCaptureService::suppressNextChange()`; connects tray pause/resume to
capture pause state; and converts tray quit into a runtime quit signal plus
optional stop.

The resident runtime remains dependency-injected by design. Tests pass fake
clipboard sources/accessors, fake paste invokers, fake hotkey backends, and
fake tray backends, so the runtime can be exercised without touching the real
system clipboard, registering a real global hotkey, creating a real system tray
icon, or sending Ctrl+V.

This is still not autostart registration, foreground-window recovery, real
focus/timing orchestration, target-window selection, rich content capture or
insertion, image insertion, or HTML/RTF/blob handling.

## Resident Host And Factory Skeleton

Pinloom Clip now has a small widgets-layer resident host/factory skeleton above
`ClipResidentRuntime`. `ClipResidentRuntimeFactory` validates required runtime
dependencies, constructs an in-memory repository by default, can open and
initialize an explicitly configured SQLite database path, and returns a
`ClipResidentHost` that owns the repository and the runtime. Tests can also
inject an already constructed in-memory or SQLite repository so no test needs
to touch user clipboard history or app data.

`ClipResidentHost` is intentionally thin. It starts, stops, and requests quit
through the held `ClipResidentRuntime`, forwards runtime signals, and exposes
the held runtime/repository for app-layer status surfaces and tests. Picker
search options and insertion options are still applied by the runtime, so the
host/factory layer does not bypass suppression, capture pause, tray show, or
picker insertion behavior.

A default-platform host builder is the production construction hook used by
`pinloom_app.exe`. The test suite continues to use fake clipboard
source/accessor, fake paste invoker, fake hotkey backend, and fake tray backend,
so tests do not register real global hotkeys, create a real system tray icon,
touch the real clipboard, or send Ctrl+V.

This is still not autostart registration, foreground-window recovery, real
focus/timing orchestration, target-window selection, rich content capture or
insertion, image insertion, or HTML/RTF/blob handling.

## Resident App Entry And Config Skeleton

Pinloom Clip now has a small widgets-layer `ClipResidentApp` and
`ClipResidentAppConfig` skeleton above `ClipResidentHost` and
`ClipResidentRuntimeFactory`. The app skeleton stores and validates resident
configuration, selects the repository kind and explicit SQLite database path,
applies hotkey, picker search, insertion, tray visibility, and quit behavior
options, creates the host through an injected host builder/factory, and exposes
start/stop/requestQuit, status, and lastError for a future executable entry.

The default config remains in-memory so it does not touch real user clipboard
history or app data. SQLite use requires an explicit database path, and tests
use temporary paths only. The resident app tests continue to inject fake
clipboard source/accessor, fake paste invoker, fake hotkey backend, and fake
tray backend, so they do not register real global hotkeys, create a real system
tray icon, touch the real clipboard, or send Ctrl+V.

This layer deliberately still goes through `ClipResidentHost` and
`ClipResidentRuntime`: hotkey and tray show requests, capture pause/resume,
picker activation, insertion suppression, and quit handling remain owned by
the lower runtime composition instead of being reimplemented in the app
skeleton.

This is still not autostart registration, foreground-window recovery, real
focus/timing orchestration, target-window selection, rich content capture or
insertion, image insertion, or HTML/RTF/blob handling.

## Resident App Config Persistence Skeleton

Pinloom Clip now has a small widgets-layer JSON config persistence skeleton for
the resident app. `ClipResidentAppConfigStore` can save and load
`ClipResidentAppConfig` from an explicitly supplied JSON file path, including
the repository kind, explicit SQLite database path and initialization flag,
hotkey key/modifiers, picker search options, insertion options, and the current
tray/picker/quit behavior booleans.

The store deliberately has no implicit default location and does not read or
write the user's real app data directory. Missing explicit config files load
the safe default in-memory resident config, while invalid JSON, unknown
repository kinds, invalid hotkeys, invalid field types, and write failures
return clear errors. Tests use `QTemporaryDir` paths only. A thin helper can
load a config through the store and pass it to `ClipResidentApp::configure()`
without starting the resident runtime or touching tray, hotkey, or clipboard
services.

This is still not a settings UI, autostart registration, an installed resident
application entry, user-profile config migration, cloud sync, policy UI, or a
final persistence contract for future settings.

## Saved Clip Import/Export Skeleton

Pinloom Clip now has a small core `ClipArchive` JSON import/export skeleton for
Saved Clips. It is a portability helper, not a background sync layer: callers
must pass an explicit JSON file path for every import or export, and the core
does not choose a default app-data location.

The archive intentionally exports Saved Clips only. Temporary Clip History is
private working memory and is not exported by default; exported files include
`temporaryHistoryExported: false` as an explicit privacy signal. The current
schema is versioned as `pinloom.clip.savedClips` version `1`.

The MVP archive is text-only. It includes the saved clip id, saved state, text
kind, text and preview, saved name, aliases, tags, pinned state, timestamps,
source app metadata, content hash, and byte size. It does not include rich
content blobs, images, HTML, RTF, files, real clipboard data, tray UI state, or
hotkey/runtime settings.

Import preserves clip ids when possible. The conservative conflict policy is:
if a clip with the imported id already exists, skip that archive entry and
leave the existing local clip unchanged. New saved clips are imported into the
repository as Saved state, with derived text metadata normalized on import.
Invalid JSON, non-object archives, unsupported schema/version values, malformed
clip entries, and repository insert failures return explicit errors.

## Main App Integration MVP

`pinloom_app.exe` now starts the Clip resident runtime alongside the main
Pinloom panel. It uses the same app-data directory as the launcher database and
stores clips in:

```text
pinloom_clip.sqlite3
```

The runtime uses the system tray menu, Qt clipboard capture, SQLite persistence,
and the platform paste invoker. The main app deliberately disables the
standalone Clip hotkey registration so `Ctrl+Space` is the single command
entry. Copy plain text in any application with `Ctrl+C` to add it to temporary
history, summon the launcher with `Ctrl+Space`, type `c s` or `c s <query>`,
and press Enter to paste the selected text into the current application. Type
`c n` to choose a recent temporary history item and save it as a named Saved
Clip with aliases, tags, and pinned state. The tray menu's `Show Clipboard`
action routes back to the same `c s` launcher path instead of making the
standalone picker the primary workflow, and `Quit Pinloom` exits the resident
app.

`pinloom_app.exe` also registers a separate `Ctrl+Space` global hotkey for the
main Pinloom launcher. That hotkey restores/raises a compact horizontal search
bar and focuses the search box. On Windows `Ctrl+Space` can conflict with IMEs
or another application that already registered the same shortcut.

The legacy standalone picker still exists as a support widget, but the daily
path is the main launcher command surface. `c s` includes temporary history and
Saved Clips for insertion. `c n` shows only temporary history as save
candidates; once a temporary clip is saved, it is no longer mixed into
temporary history. Saved Clips are still searchable by name, alias, and `#tag`.
Re-copying exact text already stored as a Saved Clip is ignored as a duplicate
by the content hash check.

The main `Ctrl+Space` launcher uses the same `pinloom_clip.sqlite3` repository
as the resident Clip picker. Its default surface is a single focused search
bar with a compact results dropdown. Explicit searches can return anchors and
Saved Clips; Saved Clip rows are labeled as clips and show concise metadata
such as tags, aliases, timestamp, and preview. Pressing Enter on a Saved Clip
in the main launcher inserts the text clip into the current foreground
application through `ClipInsertionService::insertClip(clipId)`. Pressing Enter
on an anchor result still follows the normal native jump path.

Current limits remain explicit:

- The MVP captures and inserts text only.
- Native foreground-window recovery, paste timing polish, target-window
  selection, rich text, images, files, HTML/RTF/blob handling, and autostart are
  still future work.

## Privacy And Limits

Privacy constraints are part of the first implementation layer:

- blank or whitespace-only content is ignored
- maximum text byte size is enforced before storage
- duplicate text is ignored by content hash
- capture can be paused through policy
- source application exclusion is modeled in policy
- obvious marker-style sensitive text is ignored by default before storage,
  including markers such as `password=`, `passwd:`, `api_key`, `secret=`,
  `token=`, `authorization: bearer`, and private key headers
- callers can add case-insensitive substring markers for local policy needs or
  explicitly disable the built-in sensitive-text filter
- temporary history can be pruned by TTL and maximum count
- saved clips do not expire through temporary-history retention

The sensitive-text filter is a conservative skeleton for clearly marked
clipboard content. It is not semantic secret detection, a password manager, or
a replacement for DLP controls.

Tray UI, global hotkey capture, floating picker selection, and blacklist
persistence should continue to call into these same policy checks before storing
anything.

## Main Branch Status

The Clip module is now part of the local `main` branch and is wired into
`pinloom_app.exe`. Temporary Clip History remains private to Pinloom Clip and
is not added to Pinloom's main search index by default.
