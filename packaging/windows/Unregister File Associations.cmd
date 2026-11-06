@echo off
setlocal DisableDelayedExpansion
"%~dp0pinloom_app.exe" --unregister-file-associations
exit /b %errorlevel%
