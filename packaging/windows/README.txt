Pinloom Windows Release Package
===============================

Contents
--------
- pinloom_app.exe: Pinloom resident app and command UI.
- Start-Pinloom.cmd: Starts the Pinloom UI.
- Start-Pinloom-Hidden.cmd: Starts Pinloom in hidden resident mode.

PDF anchor test steps
---------------------
1. Install or extract SumatraPDF 3.7 or newer and set it as the Windows default
   PDF app. Confirm sumatrapdf-tool.exe and libmupdf.dll are next to
   SumatraPDF.exe; the single-file portable reader does not include Preview's
   renderer.
2. Open Pinloom settings and configure SumatraPDF if it is not detected.
   Optionally configure an Obsidian Vault and relative Saved Clip directory.
3. Open a PDF directly in SumatraPDF.
4. With SumatraPDF active, press Shift+Space, type k n, and press Enter.
5. Drag a rectangle inside one PDF page, name the anchor, and save it.
6. Open the anchor to verify page/scroll restoration and temporary highlighting.

Anchor Library test steps
-------------------------
1. Press Shift+Space, type a;l, and press Enter.
2. Combine the scope, tag, type, application, directory, time, and usage
   filters; save the combination as a Saved view and load it again.
3. In both the file and Anchor tables, double-click an Alias cell and click a
   Tag cell. Confirm the yellow unsaved state, press Ctrl+S, and confirm the
   green saved state.
4. Select multiple files or anchors and batch-update tags or Pinned state.
5. Use the file and Anchor right-click menus plus Delete. Open the Trash button,
   verify the themed view, restore metadata/Anchors independently, then
   permanently delete a disposable item and confirm a safety backup was created.
6. Use Relink or Auto-find for a missing local file. Use Merge for duplicate
   file records and Merge duplicate anchors for repeated locators.
7. Run Inspect and verify missing, duplicate, and invalid locator counts. Use
   the tag manager to rename and remove a test tag, then verify Undo.
8. Select a PDF Anchor and confirm the Preview-only right pane fills
   automatically without opening SumatraPDF. Select another Anchor and return
   to confirm the cached preview is immediate. Right-click the Anchor and use
   Recapture to replace its rectangle. Confirm Type shows a user-facing value.

Contextual Saved Clip test steps
--------------------------------
1. In PowerToys Keyboard Manager, remap Caps Lock to F24. Pinloom recognizes
   F24 as its private Hyper carrier. The former left Ctrl + left Alt +
   left Shift + backtick mapping remains supported for compatibility.
2. Configure an Obsidian Vault and relative Saved Clip directory in Pinloom.
   Saved Clips also work without Obsidian and remain in the local Clip database.
3. Select text in a standard Win32, UI Automation-compatible, or Scintilla
   editor such as Notepad++, then press Caps Lock + S.
   Set the Clip name, click Tags to select or create tags, then save. Pinloom
   does not use Ctrl+C or change the selection. Verify the generated Obsidian
   note filename contains the Clip name without a Pinloom ID.
4. Place the caret in an editor and press Caps Lock + V.
5. Search the directly opened Clip Picker by name or alias. Use tag;name for
   an exact tag filter, for example reference;socket, then press Enter. Pinloom
   returns focus to the original editor and inserts the selected Clip. A Clip
   tagged wb opens its single HTTP/HTTPS URL in the default browser instead.
6. Press Shift+Space and run clip;library. Verify Saved Clips, Clipboard
   History, Trash, the full-content preview, inline Alias editing, Tag selection,
   Action/Storage/Source metadata, and Ctrl+S metadata saving. Delete a test
   Clip, restore it, then permanently remove it from Trash. An Obsidian note
   must remain present with pinloom_state set to forgotten.

Storage migration test steps
----------------------------
1. Open Settings and choose an empty Data directory.
2. Restart Pinloom. Confirm pinloom.sqlite3, pinloom_clip.sqlite3, and both
   backup directories were copied and the old directory was retained.
3. Choose a non-empty destination and restart. Confirm Pinloom refuses to
   overwrite it and continues with the current data directory.

Notes
-----
- Rectangle capture requires SumatraPDF 3.7 DDE support.
- Pinloom reads the active PDF path, page, and zoom directly from SumatraPDF DDE.
- Right-click or press Esc to cancel the transparent rectangle capture layer.
- Release packaging refuses a dirty Git worktree unless -AllowDirty is passed
  explicitly to Package-Release.ps1.
