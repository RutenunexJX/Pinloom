# Paired SumatraPDF maintenance

Pinloom 0.4.17 is distributed with SumatraPDF 3.6.1, customization `pinloom.2`.
`integrations/sumatra/source.json` pins the upstream revision, exact customized
release commit and complete customization patch hash. The corresponding source
archive and build inputs accompany each paired package.
The standalone reader source remains on `codex/pinloom-native-capture` in the independent SumatraPDF checkout.
The Pinloom repository stores the customization patch and build inputs. Each
published release's `reader/source/SumatraPDF-source.zip` contains that release's
corresponding reader source and vendored dependencies; it does not include later
uncommitted candidate changes.
Do not push Pinloom customizations to the official SumatraPDF upstream remote.

## Operation

Open `reader/SumatraPDF.exe`, click Capture, drag a rectangle within one page,
and save from the adjacent card. For a text anchor, select native PDF text and
choose `Save Text Anchor to Pinloom` from its context menu. Text may span lines
and pages. A selectable text layer is required; this integration does not run OCR.
There is no PDF Text Clip action. Generic Pinloom Clip and Hyper+S are unchanged.

The card supports Name, Tags, Save, Reselect, Cancel and Connection. A known
failure retains the editable draft; an unconfirmed submission locks its content
and Retry queries the original library with the same capture ID. Closing an
unconfirmed card retains it; Capture resumes it. Unsaved drafts are persisted
under `PinloomPendingCaptures` in the reader profile. Closing the reader preserves
them; Capture restores them after reopening a PDF. A changed or unavailable
document requires reselection and retains its name and tags.

CLI discovery checks `../pinloom_cli.exe`, then `pinloom_cli.exe` beside the reader,
then a valid remembered manual path, in that order. It validates capture protocol
and text capability when needed. Stale absolute bindings cannot block a compatible
paired CLI. Discovery does not depend on the working directory or scan other disks.
Connection supports a manually selected CLI for nonstandard layouts and an
explicit library folder. Its `Use Pinloom default`, `Choose library folder` and
`Keep current library` buttons preserve those distinct choices.

New settings are written to `PinloomIntegration.user.ini` in Sumatra's active
profile (including `-appdata`, portable mode or its normal per-user profile).
Each absent key falls back to the legacy `reader/PinloomIntegration.ini`. An empty
LibraryPath explicitly selects the Pinloom default; an absent key preserves the
legacy library choice. The legacy file is not rewritten. Both files are user data
and must be preserved during upgrades. Failed writes are reported and never
claimed as a remembered binding. Native drafts also require a writable profile.
Failed draft cleanup keeps the card available instead of claiming cancellation
or silently leaving a recoverable request behind. Integration-owned messages and
buttons are English; Windows file/folder pickers retain the operating system language.
With no reader-specific library setting the CLI resolves Pinloom's configured
default. Automatic CLI discovery never changes the library setting.
Pinloom automatically uses the bundled reader for native PDF anchors, including when an older external-reader path remains in Settings. Legacy locator behavior and explicit legacy-reader settings remain supported.
Keep the entire paired package together when moving it to another computer. Pinloom library/PDF paths still need to exist on that computer.

Native text locators use `sumatrapdf.text` v3 with a source signature, ordered
native character endpoints, complete extracted UTF-8 text and first-region
geometry. The reader re-extracts and compares the exact range before acknowledging
positioning; repeated text is not located by first-match search. Limits are
64 KiB UTF-8 and 1,000 selected pages, with explicit refusal and no truncation.
Source identity remains canonical path, size and modification time. Existing
rectangle, page, search and legacy text locators remain supported.

## Build and update

1. Start from the exact upstream revision in `source.json`, then apply `pinloom-customization.patch` with `git apply --check` followed by `git apply`. To rebuild a published release instead, unpack its corresponding complete source archive, which already contains that release's patch.
2. Use the source tree's `scripts/pinloom-build.ps1 -ToolsRoot <isolated-tools>` (also archived as `Compile-Reader.ps1`) with MSVC 14.44.35207, MSBuild 17.14.60.43110 and Windows SDK 10.0.26100.0. The preserved payload inventories, filtered Visual Studio catalog and preparation scripts describe the isolated layout. Run preparation in a separate tools directory, then pass that directory as ToolsRoot.
3. Keep changes small and traceable. The Pinloom executor owns reader changes, build scripts, protocol compatibility and self-tests. The coordinator accepts the complete delivery and publishes Pinloom and its reader together.
4. Assess upstream fixes and security changes explicitly, update the pinned baseline, reapply/review custom patches and run reading, rectangle geometry, repeated/multiline/cross-page text, save/retry/cancel/recovery, library routing and anchor-reopen regression before release. Do not automatically track upstream main.
5. Stage `reader-manifest.json` with the exact source revision, paired Pinloom version and SHA-256 of every payload file. `Package-Release.ps1 -ReaderDirectory <payload>` rejects mismatched or incomplete reader payloads.

The official automatic update check is disabled in this customization. Its manual update command explains that updates come with Pinloom, preventing it from installing an uncustomized official binary.
Retain the reader's upstream license, copyright and dependency notices. The source archive, build inputs, patches and release checksums accompany each paired release. Existing Pinloom licensing is unchanged.

## Verification boundary

Release evidence and candidate evidence are separate. The candidate delivery
records exact binaries, source differences, regression results and actual GUI
flows. Cross-physical-monitor/DPI transitions and abrupt process termination at
commit are not inferred from normal exit/retry tests. The optional legacy
`sumatrapdf-tool.exe` preview renderer is not introduced by native capture.

The Win32 configuration flush call can return zero on success; the implementation
checks the actual write and reads the setting back after flushing. See Microsoft's
[WritePrivateProfileString return-value contract](https://learn.microsoft.com/en-us/windows/desktop/api/winbase/nf-winbase-writeprivateprofilestringa).
