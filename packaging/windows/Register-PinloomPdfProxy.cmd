@echo off
setlocal
set "ROOT=%~dp0"
set "PROXY=%ROOT%pinloom_pdf_proxy.exe"

if not exist "%PROXY%" (
  echo pinloom_pdf_proxy.exe was not found in "%ROOT%".
  pause
  exit /b 1
)

reg add "HKCU\Software\Classes\Applications\pinloom_pdf_proxy.exe\shell\open\command" /ve /d "\"%PROXY%\" \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\Applications\pinloom_pdf_proxy.exe\SupportedTypes" /v ".pdf" /t REG_SZ /d "" /f >nul

echo Registered Pinloom PDF Proxy as an Open With candidate:
echo %PROXY%
echo.
echo Windows Default Apps settings will open. Set .pdf to pinloom_pdf_proxy.exe.
start "" "ms-settings:defaultapps"
pause
