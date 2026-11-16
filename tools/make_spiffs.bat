@echo off
rem ============================================================================
rem  make_spiffs.bat - Build LILYGO-T-Display-S3_Nav\build\spiffs.bin
rem
rem  Double-click this file, or run from the repo root:
rem    tools\make_spiffs.bat
rem    tools\make_spiffs.bat D:\out\spiffs.bin
rem ============================================================================
setlocal

rem 先由 cmd 建立預設輸出資料夾（可避開部分系統的資料夾保護限制）
set "OUTDIR=%~dp0..\LILYGO-T-Display-S3_Nav\build"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_spiffs.ps1" %*
if errorlevel 1 goto fail
if "%~1"=="" pause
exit /b 0

:fail
echo.
echo [ERROR] Failed to build spiffs.bin. See the messages above.
pause
exit /b 1
