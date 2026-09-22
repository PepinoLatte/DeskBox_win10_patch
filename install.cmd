@echo off
rem DeskBox Round Corner Patch - Install (user-level, no admin required)
rem Usage: double click, or run from command line
setlocal EnableExtensions
echo ================================================
echo   DeskBox Round Corner Patch - Install
echo ================================================
echo.

set "PATCHDIR=%~dp0"
set "LAUNCHER=%PATCHDIR%DeskBoxRound.exe"
set "DLL=%PATCHDIR%deskbox_round.dll"

if not exist "%LAUNCHER%" (
    echo [ERROR] Missing DeskBoxRound.exe
    pause & exit /b 1
)
if not exist "%DLL%" (
    echo [ERROR] Missing deskbox_round.dll
    pause & exit /b 1
)

rem --- locate DeskBox.exe via helper script ---
for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%PATCHDIR%ps_install.ps1" -PatchDir "%PATCHDIR%"`) do set "DESKBOXEXE=%%I"
if not defined DESKBOXEXE (
    echo [ERROR] DeskBox.exe not found. Install DeskBox first.
    pause & exit /b 1
)
echo [OK] DeskBox found: %DESKBOXEXE%
for %%I in ("%DESKBOXEXE%") do set "DESKBOXDIR=%%~dpI"

rem --- rewrite Run key: DeskBox.exe -> launcher ---
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v DeskBox /t REG_SZ /d "\"%LAUNCHER%\" --startup --startup-source=run" /f >nul
if errorlevel 1 (
    echo [ERROR] Failed to write autostart registry key
    pause & exit /b 1
)
echo [OK] Autostart now points to the launcher

rem --- record original run value for uninstall ---
>"%PATCHDIR%original_run_key.txt" echo "%DESKBOXEXE%" --startup --startup-source=run

rem --- rewrite shortcuts (desktop + start menu) ---
powershell -NoProfile -ExecutionPolicy Bypass -Command "$sh = New-Object -ComObject WScript.Shell; $desk = [Environment]::GetFolderPath('Desktop'); $sm = $env:APPDATA + '\Microsoft\Windows\Start Menu\Programs\DeskBox.lnk'; $targets = @(); if (Test-Path ($desk + '\DeskBox.lnk')) { $targets += ($desk + '\DeskBox.lnk') }; if (Test-Path $sm) { $targets += $sm }; foreach ($t in $targets) { $s = $sh.CreateShortcut($t); $s.TargetPath = '%LAUNCHER%'; $s.Arguments = '\"%DESKBOXEXE%\"'; $s.IconLocation = '%DESKBOXEXE%,0'; $s.WorkingDirectory = '%DESKBOXDIR%'; $s.Save(); Write-Host ('[OK] shortcut updated: ' + $t) }; if ($targets.Count -eq 0) { Write-Host '[SKIP] no DeskBox shortcuts found' }"

tasklist /FI "IMAGENAME eq DeskBox.exe" 2>nul | find /I "DeskBox.exe" >nul
if not errorlevel 1 (
    echo [WARN] DeskBox is running - restart it via the desktop shortcut to apply
)

echo.
echo ================================================
echo   Install complete!
echo   - Launch DeskBox from the desktop shortcut
echo   - Adjust radius: edit deskbox_round.ini, restart DeskBox
echo   - Uninstall: run uninstall.cmd
echo ================================================
pause
