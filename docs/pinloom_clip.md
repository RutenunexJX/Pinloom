# Pinloom Clip

Pinloom Clip is the text capture and insertion subsystem used by the unified
Pinloom Command Window. It keeps short-lived clipboard history, stores named
Saved Clips, optionally uses an Obsidian Vault as the durable text source, and
inserts a selected Clip into the application that was active before Pinloom
opened.

## User Workflows

The primary entry point is `Ctrl+Space`:

- An ordinary query searches Anchors, Saved Clips, Inbox files, and resources.
- `c` lists Clip commands.
- `c s` or `c s <query>` searches temporary history and Saved Clips for
  insertion.
- `c n` lists temporary history items that can be promoted to Saved Clips.
- The tray `Show Clipboard` action opens the same `c s` command surface.

Pinloom also recognizes `Ctrl+Alt+Shift+backtick+S` as `Hyper+S`. The intended
PowerToys mapping is Caps Lock to `Ctrl+Alt+Shift+backtick`, making the physical
shortcut Caps Lock plus S.

- If Windows UI Automation or a standard Edit/RichEdit control exposes a
  non-empty selection, `Hyper+S` creates a Saved Clip from that selection.
- If there is only a caret, or the control does not expose a reliable
  selection, `Hyper+S` opens `c s` for insertion.
- Selection capture does not synthesize `Ctrl+C` and does not read the system
  clipboard.

## Runtime Architecture

`pinloom_app.exe` owns one Clip resident host and the unified Command Window.
The active components are:

- `ClipboardCaptureService`: observes plain-text clipboard changes, applies
  capture policy, and writes temporary history.
- `ClipRepository`: provides in-memory and SQLite implementations for temporary
  and Saved Clips.
- `ClipSearchService`: searches names, aliases, tags, previews, and text with
  deterministic ranking.
- `ClipInsertionService`: loads the latest Clip text, writes it to the
  clipboard, restores the previous foreground target, invokes paste, and can
  restore the prior clipboard value after the target processes the paste.
- `ObsidianClipStore` and `ObsidianClipSyncService`: write and synchronize one
  managed Markdown file per Saved Clip when an Obsidian Vault is configured.
- `HyperHotkeyService`: recognizes the contextual archive-or-insert chord on
  Windows.
- `ClipTrayController`, `ClipTrayPresenter`, `ClipResidentRuntime`, and
  `ClipResidentHost`: compose clipboard capture, insertion, tray state, and
  process lifetime.

There is no separate Clip picker window. Search, save, insertion, metadata
actions, and tray routing all use `PinloomCommandPanel`.

## Persistence

The resident app stores the local Clip cache in:

```text
<Pinloom app data>/pinloom_clip.sqlite3
```

Temporary history is local working memory. Saved Clips include text, name,
aliases, tags, pinned state, timestamps, source metadata, content hash, and
byte size.

When an Obsidian Vault is configured, each Saved Clip is represented by one
UTF-8 Markdown note under the configured archive directory. Managed
frontmatter includes `pinloom_id`, `pinloom_type`, `pinloom_version`, `name`,
`aliases`, `tags`, `pinned`, `created`, and `updated`. The body is the exact
insertion payload. SQLite remains the local search and usage cache; insertion
reloads the current Markdown body before pasting.

## Capture Policy

Clipboard capture applies these constraints before persistence:

- Blank or whitespace-only text is ignored.
- Maximum UTF-8 byte size is enforced.
- Exact duplicate text is ignored by content hash.
- Capture can be paused.
- Applications can be excluded by policy.
- Marker-style sensitive text is rejected by default, including common
  password, token, authorization, API key, and private-key markers.
- Temporary history is pruned by count and age; Saved Clips do not expire
  through temporary-history retention.

The sensitive-marker filter is a conservative guard, not semantic secret
detection or a replacement for endpoint data-loss controls.

## Current Limits

The current implementation captures and inserts plain text only. Rich text,
images, files, HTML/RTF payloads, semantic search, cloud synchronization, and
automatic operating-system startup are outside the current scope.
