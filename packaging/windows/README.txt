Pinloom Windows Test Package
============================

Contents
--------
- pinloom_app.exe: Pinloom resident app and command UI.
- pinloom_pdf_proxy.exe: PDF open proxy. Set this as the Windows default PDF app
  when testing enhanced PDF mode.
- Register-PinloomPdfProxy.cmd: Registers pinloom_pdf_proxy.exe as an Open With
  candidate for .pdf files and opens Windows Default Apps settings.
- Start-Pinloom.cmd: Starts the Pinloom UI.
- Start-Pinloom-Hidden.cmd: Starts Pinloom in hidden resident mode.

PDF enhanced mode test steps
----------------------------
1. Run Register-PinloomPdfProxy.cmd.
2. In Windows settings, set PDF files to open with pinloom_pdf_proxy.exe.
3. Open Pinloom settings and configure SumatraPDF if it is not detected.
   Optionally configure an Obsidian Vault and relative Saved Clip directory.
4. Double-click a PDF.
5. SumatraPDF should open the PDF, while Pinloom records the full PDF path.
6. With SumatraPDF 3.7 active, press Ctrl+Space, type k n, and press Enter.
7. Drag a rectangle inside one PDF page, name the anchor, and save it.
8. Open the anchor to verify page/scroll restoration and temporary highlighting.

Notes
-----
- Windows usually requires user confirmation before changing the default PDF app.
- If SumatraPDF is missing, pinloom_pdf_proxy.exe shows a clear error dialog.
- Rectangle capture requires SumatraPDF 3.7 DDE support.
- Right-click or press Esc to cancel the transparent rectangle capture layer.
- If you move this folder, run Register-PinloomPdfProxy.cmd again.
