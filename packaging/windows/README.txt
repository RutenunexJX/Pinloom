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
4. With SumatraPDF active, press Ctrl+Space, type k n, and press Enter.
5. Drag a rectangle inside one PDF page, name the anchor, and save it.
6. Open the anchor to verify page/scroll restoration and temporary highlighting.

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
