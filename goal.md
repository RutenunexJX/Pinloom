# Pinloom Goal

## Core Goal

Pinloom v1 is a deterministic anchor launcher for native applications.

It helps a user name an exact position in a native document or drawing, then
return to that exact position later with a compact Listary-style search box.
Pinloom should answer "which saved place do I want to jump to?" rather than
"which files contain this content?"

## Product Promise

The v1 promise is a small, repeatable loop:

1. Capture the current position in a native app.
2. Name the anchor.
3. Add optional aliases and tags.
4. Search by name, alias, or `#tag`.
5. Press Enter.
6. Land back at the same native-app position.

The same loop now applies to every Pinloom Entry that appears in the Command
Window: Anchors, Saved Clips, Inbox files, and regular local file resources can
be searched, executed with Enter, expanded with Right Arrow, renamed, retagged,
re-aliased, pinned, soft-deleted, and restored where the backing model supports
it. Unsupported actions must stay visible with a disabled reason so failure is
diagnosable.

This is a deterministic launcher, not a general content discovery system.

## Architecture Principles

- Anchor first: `Anchor` is the primary entity. Files and resources are target
  containers.
- Native app first: jump execution is delegated to the app that owns the target.
- Locator first: each anchor stores a structured locator, preferably JSON or
  equivalent structured fields, and dispatches by `target_app` and
  `locator_type`.
- Keyboard first: the main UI should feel like a command palette or Listary
  overlay, not a library dashboard.
- Entry/action first: command surfaces should call a reusable
  command -> entry/action -> result protocol with success, message,
  diagnostics, and next-UI-hint fields instead of burying product behavior in a
  Qt-only widget branch.
- Manual capture first: user-authored anchors are more important than automatic
  discovery.
- Deterministic over semantic: exact file, app, page, rectangle, range,
  bookmark, slide, or shape coordinates beat fuzzy text or semantic search.
- Preserve useful infrastructure: SQLite, aliases, tags, anchor FTS, ranking,
  recency, pinned state, and host APIs should be reused where they support the
  anchor launcher loop.
- External text source: when an Obsidian Vault is configured, Saved Clip
  Markdown is the source of truth and SQLite is a rebuildable search and usage
  cache. Pinloom remains the capture, retrieval, and insertion surface rather
  than becoming a note editor.
- Contextual Hyper layer: the PowerToys Caps Lock remap emits
  `Ctrl+Alt+Shift+backtick`; `Hyper+S` archives an accessible text selection
  without clipboard access, while caret-only or unknown selection state opens
  Saved Clip retrieval and returns insertion to the original foreground
  control.
- Freeze non-mainline expansion: source readers, content scanners, web/feed
  importers, screenshot search, OCR search, and relationship graphs are not v1
  headline work.

## Target Anchor Model

The implementation should converge on:

```text
id
name
aliases
tags
target_app
target_file or target_uri
locator_type
locator_json
pinned
created_at
updated_at
used_at
```

Search priority:

```text
exact name > alias > tag > recent/pinned > target metadata
```

## V1 Application Targets

### SumatraPDF

SumatraPDF is the v1 PDF host. Pinloom should not build a PDF reader
and should not depend on OCR to find anchors in scanned PDFs.

The user creates a PDF anchor manually. Pinloom stores page and rectangle, then
jumps with SumatraPDF command-line arguments such as `-page`, `-zoom`,
`-search`, and `-scroll`. SumatraPDF 3.7 DDE provides the active file state and
mouse positions used for same-page rectangle capture. Pinloom adds its own
short-lived, click-through rectangle highlight after a jump.

### Excel

Excel anchors should use file, sheet, and range or named range. The jump
executor should activate Excel and select the range.

### Word

Word anchors should use bookmarks. Page numbers and text snippets are not stable
enough for the v1 deterministic model.

### PowerPoint

PowerPoint anchors should use file, slide, and shape id or shape name. The jump
executor should navigate to the slide and select the shape when possible.

### Visio

Visio anchors should use file, page, and shape UniqueID. Visio is important for
engineering drawing workflows and ranks just behind PDF and Excel.

## Non-Goals For V1

- Replacing Everything.
- Becoming an Obsidian or notes system.
- Acting as a PDF reader.
- Becoming a general-purpose file/content manager beyond explicitly marked
  Resources and Anchors.
- Automatically discovering anchors from screenshots, OCR, semantic embeddings,
  web history, feeds, bookmarks, or broad file content extraction.
- Extending reader coverage unless it directly supports deterministic anchor
  capture or jump execution.

## Technical Risk

Jumping is usually easier than capture. The highest risk is reliably capturing
the current position from SumatraPDF, Office, and Visio.

The implementation should therefore build small verified loops:

- First represent anchors correctly.
- Then jump to manually entered locators.
- Then capture locators from native apps.
- Only then polish overlay ergonomics and import/export behavior.
