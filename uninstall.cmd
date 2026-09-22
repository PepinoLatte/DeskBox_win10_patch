@echo off
rem DeskBox Round Corner Patch - Uninstall (restore original state)
setlocal EnableExtensions
echo ================================================
echo   DeskBox Round Corner Patch - Uninstall
echo ================================================
echo.

set "PATCHDIR=%~dp0"
set "LAUNCHER=%PATCHDIR%DeskBoxRound.exe"

rem --- restore Run key: point back to DeskBox.exe ---
set "RUNVAL="
if exist "%PATCHDIR%original_run_key.txt" set /p RUNVAL=<"%PATCHDIR%original_run_key.txt"
if not defined RUNVAL (
    for %%I in ("%LAUNCHER%") do set "PARENTDIR=%%~dpI.."
    set "RUNVAL=\"%PARENTDIR%\\DeskBox.exe\" --startup --startup-source=run"
)
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v DeskBox /t REG_SZ /d "%RUNVAL%" /f >nul
echo [OK] Autostart restored to DeskBox.exe

rem --- extract DeskBox.exe path from run value ---
set "DESKBOXEXE="
for /f "tokens=1" %%A in (%RUNVAL%) do set "DESKBOXEXE=%%~A"
set "DESKBOXEXE=%DESKBOXEXE:"=%"
if not exist "%DESKBOXEXE%" (
    for %%I in ("%LAUNCHER%") do set "DESKBOXEXE=%%~dpI..\DeskBox.exe"
)
for %%I in ("%DESKBOXEXE%") do set "DESKBOXDIR=%%~dpI"

rem --- restore shortcuts ---
powershell -NoProfile -ExecutionPolicy Bypass -Command "$sh = New-Object -ComObject WScript.Shell; $desk = [Environment]::GetFolderPath('Desktop'); $sm = $env:APPDATA + '\Microsoft\Windows\Start Menu\Programs\DeskBox.lnk'; $targets = @(); if (Test-Path ($desk + '\DeskBox.lnk')) { $targets += ($desk + '\DeskBox.lnk') }; if (Test-Path $sm) { $targets += $sm }; foreach ($t in $targets) { $s = $sh.CreateShortcut($t); $s.TargetPath = '%DESKBOXEXE%'; $s.Arguments = ''; $s.IconLocation = '%DESKBOXEXE%,0'; $s.WorkingDirectory = '%DESKBOXDIR%'; $s.Save(); Write-Host ('[OK] shortcut restored: ' + $t) }; if ($targets.Count -eq 0) { Write-Host '[SKIP] no DeskBox shortcuts found' }"

rem --- cleanup record file ---
if exist "%PATCHDIR%original_run_key.txt" del "%PATCHDIR%original_run_key.txt"

echo.
echo ================================================
echo   Uninstall complete!
echo   - Next DeskBox start runs the original unpatched exe
echo   - If DeskBox is running, restart it to apply
echo   - You may now delete this patch folder
echo ================================================
pause
