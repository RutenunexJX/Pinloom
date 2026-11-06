# Open PDF files with Pinloom

Supported file types for this entry point: **PDF (.pdf)**. Other SumatraPDF
formats are not registered or accepted by this Pinloom entry point.

Keep the complete portable package together, including its DLLs, plugins,
pinloom_cli.exe and reader/SumatraPDF.exe.

1. Run **Register File Associations.cmd** from the package. No administrator
   privileges are required. Running it again updates the registration.
2. Right-click a PDF in Windows Explorer, choose **Open with > Choose another
   app**, and select **Pinloom**. Windows controls the default app choice;
   select its option to always use Pinloom if desired. You can also choose
   Pinloom under Windows Settings > Apps > Default apps.
3. Opening a PDF starts the paired SumatraPDF reading window directly. It does
   not open Pinloom's management window. Existing reader tabs and windows follow
   SumatraPDF's existing preferences. Starting Pinloom without a file retains
   the resident launcher and management behavior.

You can also drag PDFs onto pinloom_app.exe or pass one or more paths:

    pinloom_app.exe -- "C:\Documents\Sample report.pdf" "C:\Documents\另一个文件.pdf"

Relative paths resolve against the caller's working directory. Use -- before
file names beginning with a hyphen. Paths are passed as separate arguments,
not evaluated by a shell. The reader is always resolved beside this executable.
It does not use the Windows default PDF app or an external reader setting.

The existing configured Pinloom library determines the reader profile.
--library can explicitly select a library for an isolated invocation:

    pinloom_app.exe --library "C:\Pinloom library" -- "C:\Documents\Sample.pdf"

Reader Connection choices remain authoritative. If neither the profile nor the
legacy reader connection has a LibraryPath setting, the first file open creates
the existing reader connection setting: default-library mode for normal opening,
or the explicit library for --library. Existing connection choices and drafts
are preserved. The reader discovers the paired CLI automatically.

After moving the **complete package**, run Register File Associations.cmd in
the new location. Windows associations store an absolute executable path, so
registration must be updated after relocation. Only the most recently
registered Pinloom package owns this user-level association.

Before deleting a registered package, run **Unregister File Associations.cmd**
from that package. This removes its registration and preserves other
applications' entries. An older package cannot unregister a newer package's
registration. If Pinloom was your default app, choose a replacement using
Windows Settings. Pinloom never edits Windows UserChoice or sets the default
PDF application automatically.

The equivalent executable maintenance options are --register-file-associations
and --unregister-file-associations. They show a completion or error dialog and
return 0 on success or 2 on failure.

Missing files, unsupported formats, a missing reader, or launch failures show
an English error and return 2. Success requires confirmation from the paired
reader that the requested PDF is displayed. A password prompt, invalid PDF or
unresponsive reader may require user action and a retry after the 15-second
confirmation timeout. All file paths are checked before opening the first;
if a later reader request fails, already opened documents remain open.

Rectangle capture, text-anchor capture, naming, drafts, library routing and
Anchor return navigation continue in the paired reader. Generic Clip and
Hyper+S remain part of Pinloom's resident launcher.
