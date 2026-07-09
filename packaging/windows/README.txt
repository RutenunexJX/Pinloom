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
4. Double-click a PDF.
5. SumatraPDF should open the PDF, while Pinloom records the full PDF path.
6. Press the Pinloom hotkey while SumatraPDF is active and create a PDF anchor.

Notes
-----
- Windows usually requires user confirmation before changing the default PDF app.
- If SumatraPDF is missing, pinloom_pdf_proxy.exe shows a clear error dialog.
- If you move this folder, run Register-PinloomPdfProxy.cmd again.
