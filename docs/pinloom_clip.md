# Pinloom Clip

Pinloom Clip is the text capture and insertion subsystem used by the unified
Pinloom Command Window. It keeps short-lived clipboard history, stores named
Saved Clips, optionally uses an Obsidian Vault as the durable text source, and
inserts a selected Clip into the application that was active before Pinloom
opened.

## User Workflows

The primary entry point is `Shift+Space`:

- An ordinary query searches Anchors, Saved Clips, Inbox files, and resources.
- `c` lists Clip commands.
- `clip;search` or `clip;search <query>` searches temporary history and Saved Clips for
  insertion.
- `clip;new` lists temporary history items that can be promoted to Saved Clips.
- `clip;library` opens Saved, History, and Trash management.
- `clip;pdf-text` captures the selected text from the SumatraPDF target that
  was remembered before the Command Window opened. The same operation is
  available as the `PDF Text Clip` button below the input.
- The tray `Show Clipboard` action opens the direct Clip Picker.
- Dropping plain text on the Command Window opens the same Saved Clip metadata
  flow, including name and tag selection.

Semicolon-separated `domain;action` is the canonical command grammar. The
command registry exposes one visible row per stable command ID; aliases are
input-only compatibility forms and never create duplicate rows. Colon forms
remain accepted for transition compatibility. Whitespace domain/action forms
are ordinary unified-search text.

The intended PowerToys mapping is Caps Lock to `F24`. Pinloom uses `F24` as a
private Hyper carrier; the former `Ctrl+Alt+Shift+backtick` carrier remains a
compatibility path.

- If Windows UI Automation or a standard Edit/RichEdit control exposes a
  non-empty selection, `F24+S` opens metadata confirmation and creates a Saved
  Clip from that selection.
- If the control does not expose a reliable selection, saving fails explicitly.
- `F24+V` opens the lightweight Saved Clip Picker at the insertion target.
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

The lightweight Clip Picker handles rapid `F24+V` retrieval. The separate Clip
Library handles Saved, History, and Trash inspection, full-text preview,
metadata editing, source-note opening, restore, and permanent removal. Ordinary
queries remain available in the unified Command Window.

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
frontmatter includes stable identity and state, name, aliases, tags, explicit
action, source metadata, pinned state, and timestamps. The body is the exact
insertion payload. SQLite remains the local search and usage cache; insertion
reloads the current Markdown body before pasting.

The SQLite database is local application state. It must not be opened by two
computers through a live synchronized folder. Multi-computer Saved Clip content
can use a synchronized Obsidian vault while each computer maintains its own
SQLite index. Anchor synchronization is not implemented.

## Capture Policy

Clipboard capture applies these constraints before persistence:

- Blank or whitespace-only text is ignored.
- Maximum UTF-8 byte size is enforced.
- Exact duplicate text is ignored by content hash.
- Automatic clipboard history is disabled until the user explicitly enables
  it, and can be paused later.
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
