Pinloom Portable Release
========================

Run `pinloom_app.exe` directly. Keep all DLLs and plugin directories beside the
executable. The package contains no installer, archive, checksum manifest,
launcher script, or user database.

Build a Release target first, then run `Package-Release.ps1`. The script removes
and recreates the fixed `Pinloom` directory when `-ReplaceExisting` is supplied.
It deploys the required Qt and MinGW runtime closure and retains only the SQLite
Qt database driver. A dirty Git worktree is rejected unless `-AllowDirty` is
provided explicitly for diagnostics.

Only ELA builds are supported. Reconfigure after committing so the build cache
records the clean source revision, then rebuild and test before packaging.
release-metadata.json and SHA256SUMS.txt are emitted beside the package.
They do not modify the suite-wide manifest. Build into a new staging root for
coordinated releases; the coordinator publishes it after reviewing the receipt.
