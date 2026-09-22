param([string]$PatchDir)
# ps_install.ps1 - locate DeskBox.exe: registry (HKCU/HKLM uninstall) -> parent dir fallback
$paths = @(
    'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\DeskBox',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\DeskBox'
)
foreach ($p in $paths) {
    try {
        $loc = (Get-ItemProperty -Path $p -ErrorAction Stop).InstallLocation
        if ($loc -and (Test-Path (Join-Path $loc 'DeskBox.exe'))) {
            Write-Output (Join-Path $loc 'DeskBox.exe')
            exit 0
        }
    } catch {}
}
# fallback: patch dir parent
$parent = Split-Path -Parent ($PatchDir.TrimEnd('\'))
$cand = Join-Path $parent 'DeskBox.exe'
if (Test-Path $cand) { Write-Output $cand; exit 0 }
exit 1
