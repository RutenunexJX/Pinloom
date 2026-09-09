# Pinloom Plan

Current version: `0.4.0`. Schema version: `16`.

## Current baseline

The resident launcher, canonical semicolon commands, native Anchor confirmation, global File/Clip/Anchor
identities, viewer-independent PDF adapter, annotated-copy PDF highlighting and semantic UI are implemented.
SumatraPDF is the implemented PDF adapter; an adapter boundary does not imply that every viewer is supported.
Current usage and build instructions are in [readme.md](readme.md); product boundaries are in [goal.md](goal.md).

## Maintenance and future scope

- Exercise migrations on disposable database copies and verify identity, alias, tag and ranking preservation.
- Keep foreground capture, cancellation and insertion reliable; report failures without modal loops.
- Extend native capture only where a stable interface and a verified locator/jump round trip exist.
  Manual structured locator creation remains an explicit fallback.
- Preserve Obsidian identity, latest-body insertion and race-safe clipboard restoration; richer payloads
  require a concrete use case beyond the existing plain-text contract.
- Keep temporary history bounded; avoid broad content crawling and private application data access.
- Verify clean-profile first run, adapter discovery, runtime dependencies and upgrade behavior on release.
  These are recurring verification requirements, not claims that packaging is still unimplemented.

Do not create a second Anchor model or delete source documents when deleting Pinloom metadata.
