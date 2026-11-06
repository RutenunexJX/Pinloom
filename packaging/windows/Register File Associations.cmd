@echo off
setlocal DisableDelayedExpansion
"%~dp0pinloom_app.exe" --register-file-associations
exit /b %errorlevel%
