@echo off
rem ============================================================================
rem  make_spiffs.bat - Build + verify LILYGO-T-Display-S3_Nav\build\spiffs.bin
rem
rem  Double-click this file, or run from the repo root:
rem    tools\make_spiffs.bat
rem    tools\make_spiffs.bat D:\out\spiffs.bin
rem    tools\make_spiffs.bat -Flash        (build, then write over USB)
rem
rem  The partition size and flash offset are read from partitions.csv, and the
rem  finished image is verified (size + SPIFFS magic + file list) before the
rem  script reports success.
rem ============================================================================
setlocal

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_spiffs.ps1" %*
if errorlevel 1 goto fail
if "%~1"=="" pause
exit /b 0

:fail
echo.
echo [ERROR] Failed to build / verify spiffs.bin. See the messages above.
pause
exit /b 1
