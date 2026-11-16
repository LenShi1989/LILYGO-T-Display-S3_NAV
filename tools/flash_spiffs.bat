@echo off
rem ============================================================================
rem  flash_spiffs.bat - Write build\spiffs.bin into the spiffs partition (USB)
rem
rem  Run from the repo root, or double-click:
rem    tools\flash_spiffs.bat                 build the image if missing, flash it
rem    tools\flash_spiffs.bat COM5            use a specific serial port
rem    tools\flash_spiffs.bat -Rebuild        rebuild before flashing
rem    tools\flash_spiffs.bat -DryRun         only print the esptool command
rem
rem  The offset (0x610000 for this project) comes from partitions.csv.
rem ============================================================================
setlocal

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash_spiffs.ps1" %*
if errorlevel 1 goto fail
if "%~1"=="" pause
exit /b 0

:fail
echo.
echo [ERROR] Flashing failed. See the checklist above.
pause
exit /b 1
