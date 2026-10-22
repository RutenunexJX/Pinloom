# Pinloom CLI

`pinloom_cli.exe` is the public, headless entry point. `pinloom_app.exe` retains deep links, `--hidden`, and `--package-check`. Product version and database schema are unchanged.

```powershell
.\pinloom_cli.exe --help
.\pinloom_cli.exe capabilities
.\pinloom_cli.exe schema
.\pinloom_cli.exe library.init --library 'D:\Pinloom data'
.\pinloom_cli.exe session.status --library 'D:\Pinloom data'
.\pinloom_cli.exe anchor.create --library 'D:\Pinloom data' --input create.json
```

`create.json` (UTF-8):

```json
{
  "name": "Design entry",
  "aliases": ["entry point"],
  "tags": ["design"],
  "pinned": true,
  "targetFile": "sample.txt",
  "locator": {"type": "file.line", "line": 2},
  "ifVersion": "absent"
}
```

The target file must exist. Relative paths are resolved against the **CLI caller's working directory**, including requests routed to a GUI. `--input -` reads JSON from standard input. The CLI never reads the system clipboard, pastes text into applications, or indexes an entire drive.

All output is one UTF-8 JSON object on stdout. Qt diagnostics use stderr. The envelope includes `protocol`, `requestId`, `library`, `session`, `ok`, `exitCode`, and `result` or `error`. `persisted` describes a completed persistence operation; `guiAdopted` means the connected GUI has refreshed its existing views. It does not mean an external target was located. Read `view.inspect` to inspect displayed rows and `operation.get` to inspect navigation.

## Commands

Run `schema` for required fields, allowed values, types, and bounds. Unknown parameters are rejected.

| Capability | Methods | Main parameters |
| --- | --- | --- |
| Discovery and session | `capabilities`, `schema`, `library.init`, `session.resolve`, `session.status`, `session.close` | Library is explicit except discovery and `session.resolve` |
| Native PDF capture | `capture.pdf`, `capture.status` | Versioned capture protocol, UUID receipt, source identity and native page-space geometry |
| Named anchors | `anchor.create`, `anchor.get`, `anchor.update`, `anchor.validate`, `anchor.delete`, `anchor.restore`, `anchor.purge` | `resourceId`, `id`, `name`, `aliases`, `tags`, `pinned`, `targetApp`, `targetFile`, `targetUri`, `locator` |
| File/resource metadata | `resource.get`, `resource.update`, `resource.delete`, `resource.restore`, `resource.purge` | `id`, `title`, `aliases`, `tags`, `pinned` |
| Search and trash | `search` | `query`, `kind` (`all`, `anchors`, `files`, `clips`), `scope` (`active`, `trash`, `all`), `tags`, `limit` |
| Saved Clips | `clip.create`, `clip.get`, `clip.update`, `clip.delete`, `clip.restore`, `clip.purge` | `id`, `name`, `text`, `aliases`, `tags`, `pinned`, `action` |
| Bounded existing history | `history.list`, `history.promote`, `history.prune` | `limit`; promotion uses `id`, `name`; prune uses `keep`, `ttlSeconds` |
| Explicitly selected Inbox files | `inbox.add` | `path`, `mode` (`link`, `copy`, `move`), `destination`, metadata |
| Registered roots | `root.list`, `root.register`, `root.update`, `root.remove`, `root.query`, `root.files` | `id`, `path`, `displayName`, `enabled`, `ignoredDirectoryNames`, `relativePath`, `limit` |
| Anchor/resource archive | `library.export`, `library.import` | `path`, export `overwrite`, import `mode` (`merge`, `replace`) |
| Resolve and navigate | `entry.resolve`, `entry.open`, `operation.get`, `operation.cancel` | `resourceId`, optional `anchorId`; operation `id` |
| GUI views | `view.open`, `view.inspect` | `view` (`anchors`, `anchors_trash`, `clips`, `history`, `trash`, `roots`, `command`), optional query or IDs |

Tags are the product's classification model. Stable IDs survive updates and trash/restore. Permanent deletion requires an item already in trash. Anchor versions cover their entire parent resource, so concurrent changes to another anchor on the same resource also conflict. Purging an Inbox entry removes metadata; it does not delete the target file. Root removal unregisters a directory; it does not delete it. `root.files` enumerates one directory inside an enabled registered root, excludes ignored directories and symbolic links, and never recurses.

To add an anchor to an existing parent, supply its `resourceId` and version from `resource.get`. Without a `resourceId`, `anchor.create` creates a parent and returns both IDs. Changing a locator and metadata is one transaction. Local target paths and structured locators use existing product validators. Web targets accept HTTP/HTTPS URIs and file targets accept local paths/file URIs.

## Versions, preview, and errors

Every data mutation requires `ifVersion` (or `--if-version`). Use `absent` only to create a new object, the exact `version` returned by a get/list for an existing object, and the `session.status` version for import/history pruning. After a mutation, use its newly returned version. There is no force-write switch. `library.init` is an explicit, idempotent initialization command; export requires `overwrite: true` to replace an existing archive.

`dryRun: true` (or `--dry-run`) validates the version and previews mutations without writing library state, copying/moving Inbox files, or writing an archive. Resource/anchor/import previews run existing domain services against an in-memory snapshot with the shared identity rules. Clip previews validate a local snapshot; they do not validate Obsidian filesystem permissions or promise that an external archive write will succeed. Dry-run output is a proposal, not a new persisted version. Fetch the actual object after committing. Export uses the existing atomic save service; import validates the complete input before applying a transaction and retains existing database safety backups.

Exit codes: `0` success, `2` invalid input/limit, `3` missing object, `4` version/identity/state/GUI-edit conflict, `5` session/transport unavailable, `6` storage/domain operation failure. Domain errors retain their diagnostic message. A transport `outcome_unknown` means the reply was lost after connection: inspect the ID/version before retrying a write. Requests are not automatically replayed offline.

Requests are limited to 1 MiB, responses to 8 MiB, import archives to 16 MiB, Clip text to 256 KiB UTF-8, and list limits to 200. Search results indicate truncation; narrow the query to retrieve additional matches. Archive export is for the anchor/resource library, matching the existing GUI format. Saved Clips retain the existing local/Obsidian persistence contract.

## Using an already running GUI

```powershell
.\pinloom_app.exe --library 'D:\Pinloom data' --no-global-hooks
.\pinloom_cli.exe session.status --library 'D:\Pinloom data' --require-gui
.\pinloom_cli.exe view.open --library 'D:\Pinloom data' --input view.json --require-gui
```

`view.json`: `{"view":"anchors","resourceId":"ID","anchorId":"ID"}`. A custom library keeps settings in `profile.ini` inside that directory. When selecting the current default data directory without a profile file, existing native Pinloom settings are reused. Explicit library sessions have separate IPC identities and do not register over the default Suite provider. `--no-global-hooks` prevents the clipboard resident host and global hotkeys from starting; the full application and library views still run.

The CLI first routes to the exact library's GUI. Otherwise it acquires an exclusive per-library process lock before opening repositories. If a process owns the lock but cannot answer, the CLI returns `session_busy`; it never falls back to a concurrent offline write. `--require-gui` makes absence of that session an error. Use this option when working with an existing GUI. Older Pinloom binaries do not provide this new protocol; only a GUI built with this interface can adopt these requests.

Pending inline edits, root metadata edits, Inbox selections, and active modal dialogs block CLI mutations and view changes. `session.status` exposes `writesBlocked` and the reason. Save or cancel edits in the GUI, fetch the current version, then retry. Requests are rejected, not queued for later overwrite. `session.close` also respects this guard. No new hotkeys are introduced.

## Native SumatraPDF capture

`session.resolve` is the only data-session lookup allowed without `--library`. It returns the canonical configured default library, `captureProtocol: pinloom-pdf-capture/v1`, and the sibling GUI executable without opening a database. Supplying `--library` resolves that explicit directory instead.

`capture.pdf` requires a GUI session and shares `AnchorCaptureCommitService` with GUI capture. Its parameters are `captureProtocol`, a UUID `captureId`, absolute `targetFile`, string `documentSize` and `documentModifiedMs` (Unix milliseconds), `name`, optional string-array `tags`, `ifVersion: absent`, and `locator`. The locator must be `sumatrapdf.rect` version 3, `unit: pt`, `coordinateSpace: page-top-left`, with one-based `page`, `rect: [left,top,right,bottom]`, original PDF `mediaBox`/`cropBox`, intrinsic `rotation` (0/90/180/270), and `userUnit`. Rectangles use the rotated, cropped page's top-left origin in physical PDF points; boxes retain PDF document coordinates. Zoom, view rotation and screen DPI are not persisted as selection coordinates.

Use `--start-gui` to connect to the exact selected library or start the sibling `pinloom_app.exe --library ... --hidden --no-global-hooks`. The helper does not show the management window or enable clipboard/global-key hooks. Startup has a bounded timeout; it does not fall back to another library or an offline writer. A connected request is never automatically repeated.

`capture.pdf` returns `state: saved` only after commit; dry-run returns `validated`. Each capture UUID owns one anchor. Captures from the same original PDF reuse its active resource while preserving existing metadata and anchors. Repeating the same normalized request returns its persisted IDs and `replayed: true`; a changed payload under an existing UUID is rejected. After an uncertain transport result, query `capture.status` with the same UUID and library before resubmitting the unchanged request. Pending GUI edits reject saves and preserve the reader's draft. Receipts live with the anchor; explicit permanent deletion removes the receipt.

Native captures reopen the original PDF through the matching custom SumatraPDF executable and `pinloom-reader/v1`. The adapter reports `located`/`positionVerified: true` only after the reader applies the page-space selection and acknowledges it. A changed source, incompatible reader or active capture returns a failure. Reader startup uses a per-library `reader-profile` directory. Configure the custom SumatraPDF executable in Pinloom's existing application settings. Existing locators keep their previous adapter behavior.

Legacy page navigation and annotated-copy previews use the custom reader's read-only `page-state` query when available, matching its executable and active PDF path. The query does not switch tabs. Readers without this bridge retain DDE verification. Legacy success continues to report `open_requested` with `positionVerified: false`; a page match alone does not prove a text search or rectangle display.

## Navigation evidence

`entry.resolve` works headlessly. `entry.open` requires a GUI and returns a session-scoped operation ID. Poll `operation.get`. Queued operations can be cancelled; active PDF requests can also be cancelled through the existing adapter. Already dispatched external applications cannot be recalled.

| Target | Supported locator types | Reported completion |
| --- | --- | --- |
| Built-in text preview | `file.line`, `zeroslack.source` | `located` only after the requested document block is loaded and selected; out-of-range lines report failure |
| File, folder, web | `manual`, `url.fragment`, existing text heading/block targets | `open_requested`; external view position is not observed |
| SumatraPDF legacy locators | `pdf.page`, `pdf.region`, `sumatrapdf.page`, `sumatrapdf.rect`, `sumatrapdf.search` | Asynchronous adapter result or failure; successful acknowledgment remains `open_requested`, with `positionVerified: false` |
| SumatraPDF native capture | `sumatrapdf.rect` with `sumatra-pinloom/v1` provenance | `located`, with `positionVerified: true`, after the matching custom reader acknowledges the selection |
| Excel | `excel.range`, `excel.name` | Existing adapter launch result; external position unverified |
| Word | `word.bookmark` | Existing adapter launch result; external position unverified |
| PowerPoint | `powerpoint.shape` | Existing adapter launch result; external position unverified |
| Visio | `visio.shape` | Existing adapter launch result; external position unverified |

PDF/Office need their existing external applications and locator-specific fields. The CLI introduces no OCR or new adapters. Saved Clips can be created and managed but have no CLI automatic-paste command.

## Portable example

In a deployed directory containing both executables and their Qt runtime:

```powershell
& .\docs\cli_workflow.ps1 -BinDirectory .
& .\docs\cli_workflow.ps1 -BinDirectory . -WithGui
& .\docs\cli_session_routing.ps1 -BinDirectory .
```

The example creates its own temporary Unicode/space-path dataset, uses only synthetic text, validates conflicts and dry runs, records every public request result in `commands.jsonl`, and writes `summary.json`. `-WithGui` starts the real main program, verifies displayed rows and line navigation, closes only that sample session, and verifies persisted data headlessly. It requires no source checkout, Python, SDK source, private documents, or developer environment variables.

Keep `cli_process.ps1` beside both example scripts. All CLI calls, including GUI readiness probes and cleanup, explicitly decode native stdout/stderr as UTF-8 and use the process exit code. They do not change PowerShell's console encoding or require a console window. The session-routing example verifies two isolated GUI libraries.
