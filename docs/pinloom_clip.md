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

This is still not the tray app, global hotkey flow, floating picker, rich text
capture, image capture, or blob storage.

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
the clipboard and sending Ctrl+V are still app-layer responsibilities for the
future tray/hotkey/picker flow. The platform invoker deliberately does not
bypass the insertion service's suppression or optional clipboard restore logic.

This is still not the tray app, global hotkey flow, Listary-style floating
picker, rich text insertion, image insertion, HTML/RTF/blob handling, or a
full window-targeting paste workflow.

## Privacy And Limits

Privacy constraints are part of the first implementation layer:

- blank or whitespace-only content is ignored
- maximum text byte size is enforced before storage
- duplicate text is ignored by content hash
- capture can be paused through policy
- source application exclusion is modeled in policy
- temporary history can be pruned by TTL and maximum count
- saved clips do not expire through temporary-history retention

Tray UI, global hotkey capture, floating picker selection, and blacklist
persistence should continue to call into these same policy checks before storing
anything.

## Worktree Isolation

This module was started in the manual git worktree:

```text
E:\Pinloom\Pinloom-clip
```

on branch:

```text
feature/pinloom-clip
```

It is not merged into `main`, does not push to `origin/main`, and does not add
Temporary Clip History to Pinloom's main search.
