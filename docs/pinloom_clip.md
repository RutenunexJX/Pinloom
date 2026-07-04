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

## Privacy And Limits

Privacy constraints are part of the first implementation layer:

- blank or whitespace-only content is ignored
- maximum text byte size is enforced before storage
- duplicate text is ignored by content hash
- capture can be paused through policy
- source application exclusion is modeled in policy
- temporary history can be pruned by TTL and maximum count
- saved clips do not expire through temporary-history retention

Future platform clipboard listeners, tray UI, global hotkey capture, and
blacklist persistence should call into these same policy checks before storing
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
