# PDF-XChange Validation

Pinloom's PDF-XChange executor launches stored PDF anchors. It does not inspect
PDF-XChange's current view and it does not provide a built-in PDF reader.

The Phase 4 UX now routes `Ctrl+K` and the PDF `Add Anchor` button through
`Capture PDF Anchor`. The normal user flow asks for anchor metadata only:
name, aliases, tags, and pinned state. The locator is inferred from the current
Pinloom selection. If the selected item is a PDF resource or PDF-XChange
anchor, Pinloom uses that PDF file and records a selected-PDF fallback locator.
When native PDF-XChange page/selection/rectangle capture is unavailable, this
fallback uses page 1 and an approximate full-page rectangle. Raw file/page/rect
fields remain in the dialog only under the advanced locator fallback.

Automatic reading of PDF-XChange's current page, view, selection, annotations,
or live coordinates remains a later research item.

## Automated Coverage

The current automated tests cover:

- `pdfxchange.rect` command building with page, zoom, highlight rectangle, and
  `usept=yes`.
- Legacy PDF page anchors that fall back to page and zoom actions.
- Missing target-file diagnostics.
- `PINLOOM_PDFXCHANGE_PATH` executable resolution.
- Widget activation through an injected PDF-XChange launcher.
- Missing executable reporting in the launcher UI.
- Manual PDF-XChange rect capture contract construction, missing input
  diagnostics, stable locator JSON, and executor compatibility.
- Manual PDF rect anchor creation service validation for missing name/file,
  invalid page/rect, repository save failure, successful persistence, search by
  name/alias/tag, stable locator JSON, executor compatibility, and SQLite
  reopen/read-back.
- No-PDF-context guidance for `Ctrl+K`.
- Selected-PDF fallback capture that creates a searchable `pdfxchange.rect`
  using only name/alias/tag/pinned metadata entry.
- The built-in capture dialog keeps raw coordinate controls hidden until the
  advanced locator fallback is opened.
- Legacy injectable manual PDF hooks still work for tests/hosts, including
  success, cancellation, invalid request errors, and launcher result
  selection/search visibility.

Run the automated checks with:

```powershell
$env:PATH='E:\QT6\Tools\mingw1310_64\bin;' + $env:PATH
E:\QT6\Tools\CMake_64\bin\cmake.exe --build build
E:\QT6\Tools\CMake_64\bin\ctest.exe --test-dir build --output-on-failure
```

## Local Executable Diagnostics

Pinloom resolves PDF-XChange in this order:

1. `PINLOOM_PDFXCHANGE_PATH`
2. `C:\Program Files\Tracker Software\PDF Editor\PDFXEdit.exe`
3. `C:\Program Files (x86)\Tracker Software\PDF Editor\PDFXEdit.exe`
4. `C:\Program Files\PDF-XChange Editor\PDFXEdit.exe`
5. `C:\Program Files (x86)\PDF-XChange Editor\PDFXEdit.exe`

Useful local checks:

```powershell
$env:PINLOOM_PDFXCHANGE_PATH
Get-Command PDFXEdit.exe -ErrorAction SilentlyContinue | Select-Object Source
$paths = @(
  'C:\Program Files\Tracker Software\PDF Editor\PDFXEdit.exe',
  'C:\Program Files (x86)\Tracker Software\PDF Editor\PDFXEdit.exe',
  'C:\Program Files\PDF-XChange Editor\PDFXEdit.exe',
  'C:\Program Files (x86)\PDF-XChange Editor\PDFXEdit.exe'
)
foreach ($path in $paths) {
  if (Test-Path -LiteralPath $path) {
    Get-Item -LiteralPath $path | Select-Object FullName,Length,LastWriteTime
  }
}
Get-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
                 'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' `
  -ErrorAction SilentlyContinue |
  Where-Object { $_.DisplayName -match 'PDF-XChange|Tracker Software' } |
  Select-Object DisplayName,DisplayVersion,InstallLocation,DisplayIcon
```

If PDF-XChange is installed in a portable or non-standard location, set:

```powershell
$env:PINLOOM_PDFXCHANGE_PATH='C:\Path\To\PDFXEdit.exe'
```

## Manual Launch Check

After `PDFXEdit.exe` is available, create or choose a safe local PDF and run a
direct command equivalent to Pinloom's generated argument list:

```powershell
$pdf = 'C:\Path\To\sample.pdf'
$exe = $env:PINLOOM_PDFXCHANGE_PATH
if (-not $exe) { $exe = 'C:\Program Files\Tracker Software\PDF Editor\PDFXEdit.exe' }
Start-Process -FilePath $exe -ArgumentList @('/A','page=1;zoom=150;highlight=72,72,220,120;usept=yes',$pdf)
```

Expected result: PDF-XChange opens the target PDF at page 1 with the requested
zoom and highlight or equivalent visible region. If the application opens the
file but ignores the rectangle, record the PDF-XChange version and the exact
command line before adjusting the command builder.

## 2026-07-04 Local Result

On this machine, no PDF-XChange executable was found through
`PINLOOM_PDFXCHANGE_PATH`, PATH lookup, common install paths, uninstall
registry entries, or Start Menu shortcuts. The end-to-end GUI launch therefore
remains environment-blocked until PDF-XChange is installed or
`PINLOOM_PDFXCHANGE_PATH` points to a valid `PDFXEdit.exe`.
