@echo off
setlocal
cd /d "%~dp0"

set "CODEX_PYTHONW=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\pythonw.exe"
if exist "%CODEX_PYTHONW%" (
  start "" "%CODEX_PYTHONW%" "tools\ofe_firmware_sign_app.py"
  exit /b 0
)

where pyw.exe >nul 2>nul
if %errorlevel%==0 (
  start "" pyw.exe -3 "tools\ofe_firmware_sign_app.py"
  exit /b 0
)

where pythonw.exe >nul 2>nul
if %errorlevel%==0 (
  start "" pythonw.exe "tools\ofe_firmware_sign_app.py"
  exit /b 0
)

echo Python 3 wurde nicht gefunden.
echo Installiere Python 3 inklusive cryptography.
pause
exit /b 1
