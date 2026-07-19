Pinloom Windows Test Package
============================

Contents
--------
- pinloom_app.exe: Pinloom resident app and command UI.
- Start-Pinloom.cmd: Starts the Pinloom UI.
- Start-Pinloom-Hidden.cmd: Starts Pinloom in hidden resident mode.

PDF anchor test steps
---------------------
1. Install SumatraPDF 3.7 or newer and set it as the Windows default PDF app.
2. Open Pinloom settings and configure SumatraPDF if it is not detected.
   Optionally configure an Obsidian Vault and relative Saved Clip directory.
3. Open a PDF directly in SumatraPDF.
4. With SumatraPDF active, press Shift+Space, type k n, and press Enter.
5. Drag a rectangle inside one PDF page, name the anchor, and save it.
6. Open the anchor to verify page/scroll restoration and temporary highlighting.

Anchor Library test steps
-------------------------
1. Press Shift+Space, type a:l, and press Enter.
2. Combine the scope, tag, type, application, directory, time, and usage
   filters; save the combination as a Saved view and load it again.
3. Double-click an Anchor Alias cell and click an Anchor Tag cell. Confirm the
   yellow unsaved state, press Ctrl+S, and confirm the green saved state.
4. Select multiple files or anchors and batch-update tags or Pinned state.
5. Use the file and Anchor right-click menus plus Delete. Open the Trash button,
   verify the themed view, restore metadata/Anchors independently, then
   permanently delete a disposable item and confirm a safety backup was created.
6. Use Relink or Auto-find for a missing local file. Use Merge for duplicate
   file records and Merge duplicate anchors for repeated locators.
7. Run Inspect and verify missing, duplicate, and invalid locator counts. Use
   the tag manager to rename and remove a test tag, then verify Undo.
8. Validate and preview a PDF locator, then use Recapture to replace its
   rectangle. Confirm Type shows a user-facing value rather than raw locator JSON.

Contextual Saved Clip test steps
--------------------------------
1. In PowerToys Keyboard Manager, remap Caps Lock to left Ctrl + left Alt +
   left Shift + backtick.
2. Configure an Obsidian Vault and relative Saved Clip directory in Pinloom.
3. Select text in a UI Automation-compatible editor and press Caps Lock + S.
   Pinloom archives it without using Ctrl+C or changing the selection.
4. Place the caret in an editor with no selection and press Caps Lock + S.
5. Search the `c s` results and press Enter. Pinloom returns focus to the
   original editor and inserts the selected Clip.

Notes
-----
- Rectangle capture requires SumatraPDF 3.7 DDE support.
- Pinloom reads the active PDF path, page, and zoom directly from SumatraPDF DDE.
- Right-click or press Esc to cancel the transparent rectangle capture layer.
