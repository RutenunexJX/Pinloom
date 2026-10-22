# Paired SumatraPDF maintenance

Pinloom 0.4.15 is distributed with SumatraPDF 3.6.1, customization `pinloom.1`.
The exact upstream and customized commits are pinned in `integrations/sumatra/source.json`.
The standalone reader source remains on `codex/pinloom-native-capture` in the independent SumatraPDF checkout.
The Pinloom repository stores the customization patch and build inputs; `reader/source/SumatraPDF-source.zip` in the release contains the complete corresponding reader source and vendored dependencies.
Do not push Pinloom customizations to the official SumatraPDF upstream remote.

## Operation

Open `reader/SumatraPDF.exe`, read a PDF, click its Capture toolbar button, select a region, name it in the adjacent card, and save. The selected-region context menu provides the same save operation.
The reader resolves `../pinloom_cli.exe` relative to itself. With no reader-specific library setting it resolves the library configured by Pinloom. Reader Settings can choose a library explicitly.
Reader-specific configuration is `reader/PinloomIntegration.ini`; it is user data, is not shipped as a managed release file, and must be preserved during upgrades. Pinloom library databases are also preserved.
Pinloom automatically uses the bundled reader for native PDF anchors, including when an older external-reader path remains in Settings. Legacy locator behavior and explicit legacy-reader settings remain supported.
Keep the entire paired package together when moving it to another computer. Pinloom library/PDF paths still need to exist on that computer.

## Build and update

1. Start from the exact upstream revision in `source.json`, then apply `pinloom-customization.patch` with `git apply --check` followed by `git apply`. Alternatively unpack the complete release source archive, which already contains the patch.
2. Use the source tree's `scripts/pinloom-build.ps1 -ToolsRoot <isolated-tools>` (also archived as `Compile-Reader.ps1`) with MSVC 14.44.35207, MSBuild 17.14.60.43110 and Windows SDK 10.0.26100.0. The preserved payload inventories, filtered Visual Studio catalog and preparation scripts describe the isolated layout. Run preparation in a separate tools directory, then pass that directory as ToolsRoot.
3. Keep changes small and traceable. The Pinloom executor owns reader changes, build scripts, protocol compatibility and self-tests. The coordinator accepts the complete delivery and publishes Pinloom and its reader together.
4. Assess upstream fixes and security changes explicitly, update the pinned baseline, reapply/review custom patches and run reading, rectangle geometry, save/retry/cancel, library routing and anchor-reopen regression before release. Do not automatically track upstream main.
5. Stage `reader-manifest.json` with the exact source revision, paired Pinloom version and SHA-256 of every payload file. `Package-Release.ps1 -ReaderDirectory <payload>` rejects mismatched or incomplete reader payloads.

The official automatic update check is disabled in this customization. Its manual update command explains that updates come with Pinloom, preventing it from installing an uncustomized official binary.
Retain the reader's upstream license, copyright and dependency notices. The source archive, build inputs, patches and release checksums accompany each paired release. Existing Pinloom licensing is unchanged.

## Verification boundary

The release records independent native capture and reopen evidence plus its own staged/installed package checks. Cross-physical-monitor/DPI transitions and crash-at-commit fault injection have not been claimed as tested. Long duplicate-name error text remains a documented nonblocking usability issue.
