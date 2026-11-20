# Onde — désinstallation (lancée par Paramètres › Applications › Onde › Désinstaller).
# Retire le programme, ses raccourcis et son inscription. Les projets (Documents\Onde) et les réglages
# (%APPDATA%\Onde) sont conservés.
param([switch] $Silencieux)
$ErrorActionPreference = 'Continue'
$dossier = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $Silencieux) {
    Add-Type -AssemblyName System.Windows.Forms
    $r = [System.Windows.Forms.MessageBox]::Show("Désinstaller Onde ?`n`nVos projets et réglages sont conservés.", 'Onde',
                                                 'YesNo', 'Question')
    if ($r -ne 'Yes') { exit 0 }
}

Get-Process Onde, onde-plugin-host, onde-stems -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($dossier, [StringComparison]::OrdinalIgnoreCase) } |
    Stop-Process -Force -ErrorAction SilentlyContinue

foreach ($lnk in @((Join-Path ([Environment]::GetFolderPath('Programs')) 'Onde.lnk'),
                   (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Onde.lnk'))) {
    Remove-Item $lnk -ErrorAction SilentlyContinue
}
Remove-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Onde' -Recurse -ErrorAction SilentlyContinue

# Ce script est dans le dossier à supprimer : la suppression est confiée à un processus qui attend sa fin.
Start-Process cmd.exe -WindowStyle Hidden -ArgumentList "/c timeout /t 2 /nobreak >nul & rmdir /s /q `"$dossier`""
if (-not $Silencieux) {
    [System.Windows.Forms.MessageBox]::Show('Onde a été désinstallé.', 'Onde', 'OK', 'Information') | Out-Null
}
