# Onde — installation pour l'utilisateur courant (sans droits administrateur).
# Lancé par Onde-Setup.exe (IExpress) depuis le dossier temporaire où il a extrait onde.zip et ce script.
#   -Destination <dossier>   emplacement (par défaut %LOCALAPPDATA%\Programs\Onde)
#   -SansRaccourcis          ni raccourcis ni entrée « Applications installées » (tests)
#   -Silencieux              sans fenêtre (tests, déploiement automatique)
param(
    [string] $Destination = (Join-Path $env:LOCALAPPDATA 'Programs\Onde'),
    [switch] $SansRaccourcis,
    [switch] $Silencieux
)
$ErrorActionPreference = 'Stop'
$ici = Split-Path -Parent $MyInvocation.MyCommand.Path
$paquet = Join-Path $ici 'onde.zip'
$version = (Get-Content (Join-Path $ici 'version.txt') -ErrorAction SilentlyContinue | Select-Object -First 1)
if (-not $version) { $version = '0.5' }

function Installer-Onde([scriptblock] $progression) {
    & $progression 'Fermeture des anciennes versions…'
    Get-Process Onde, onde-plugin-host, onde-stems -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($Destination, [StringComparison]::OrdinalIgnoreCase) } |
        ForEach-Object { $_.CloseMainWindow() | Out-Null; Start-Sleep 2; if (-not $_.HasExited) { $_ | Stop-Process -Force } }

    & $progression 'Copie des fichiers…'
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    # Les fichiers du programme sont remplacés ; les projets et réglages (ailleurs) ne sont jamais touchés.
    Expand-Archive -LiteralPath $paquet -DestinationPath $Destination -Force
    Copy-Item (Join-Path $ici 'desinstaller.ps1') $Destination -Force
    Set-Content (Join-Path $Destination 'version.txt') $version

    if (-not $SansRaccourcis) {
        & $progression 'Raccourcis…'
        $exe = Join-Path $Destination 'Onde.exe'
        $shell = New-Object -ComObject WScript.Shell
        foreach ($lnk in @((Join-Path ([Environment]::GetFolderPath('Programs')) 'Onde.lnk'),
                           (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Onde.lnk'))) {
            $s = $shell.CreateShortcut($lnk)
            $s.TargetPath = $exe
            $s.WorkingDirectory = $Destination
            $s.IconLocation = "$exe,0"
            $s.Description = 'Onde — station de travail audio'
            $s.Save()
        }
        & $progression 'Inscription dans « Applications installées »…'
        $cle = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Onde'
        New-Item -Path $cle -Force | Out-Null
        $taille = [int]((Get-ChildItem $Destination -Recurse -File | Measure-Object Length -Sum).Sum / 1KB)
        $valeurs = @{
            DisplayName = 'Onde'; DisplayVersion = $version; Publisher = 'Onde'; DisplayIcon = "$exe,0"
            InstallLocation = $Destination
            UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$Destination\desinstaller.ps1`""
        }
        foreach ($k in $valeurs.Keys) { New-ItemProperty -Path $cle -Name $k -Value $valeurs[$k] -PropertyType String -Force | Out-Null }
        foreach ($k in 'NoModify','NoRepair') { New-ItemProperty -Path $cle -Name $k -Value 1 -PropertyType DWord -Force | Out-Null }
        New-ItemProperty -Path $cle -Name 'EstimatedSize' -Value $taille -PropertyType DWord -Force | Out-Null
    }
    & $progression 'Terminé.'
}

if ($Silencieux) {
    Installer-Onde { param($t) Write-Host $t }
    Write-Host "Onde $version installé dans $Destination"
    exit 0
}

# --- Fenêtre d'installation (logo, progression, lancement) ---------------------------------------
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$fond = [System.Drawing.Color]::FromArgb(26, 29, 34)
$f = New-Object System.Windows.Forms.Form
$f.Text = 'Installation d''Onde'; $f.ClientSize = New-Object System.Drawing.Size(540, 280)
$f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false; $f.StartPosition = 'CenterScreen'; $f.BackColor = $fond
$logo = Join-Path $ici 'logo.png'
if (Test-Path $logo) {
    $pb = New-Object System.Windows.Forms.PictureBox
    $pb.Image = [System.Drawing.Image]::FromFile($logo); $pb.SizeMode = 'Zoom'
    $pb.SetBounds(28, 34, 128, 128); $f.Controls.Add($pb)
    $f.Icon = [System.Drawing.Icon]::FromHandle(([System.Drawing.Bitmap]$pb.Image).GetHicon())
}
$titre = New-Object System.Windows.Forms.Label
$titre.Text = 'onde'; $titre.ForeColor = [System.Drawing.Color]::White; $titre.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 30)
$titre.SetBounds(180, 34, 300, 56); $f.Controls.Add($titre)
$sous = New-Object System.Windows.Forms.Label
$sous.Text = "Station de travail audio · version $version"; $sous.ForeColor = [System.Drawing.Color]::FromArgb(142, 148, 156)
$sous.Font = New-Object System.Drawing.Font('Segoe UI', 10); $sous.SetBounds(184, 92, 320, 22); $f.Controls.Add($sous)
$etat = New-Object System.Windows.Forms.Label
$etat.Text = "Installation dans $Destination"; $etat.ForeColor = [System.Drawing.Color]::FromArgb(200, 204, 210)
$etat.Font = New-Object System.Drawing.Font('Segoe UI', 9.5); $etat.SetBounds(184, 122, 340, 58); $f.Controls.Add($etat)
$barre = New-Object System.Windows.Forms.ProgressBar
$barre.Style = 'Marquee'; $barre.SetBounds(184, 190, 328, 8); $f.Controls.Add($barre)
$lancer = New-Object System.Windows.Forms.Button
$lancer.Text = 'Lancer Onde'; $lancer.SetBounds(316, 226, 110, 32); $lancer.Enabled = $false
$lancer.FlatStyle = 'Flat'; $lancer.BackColor = [System.Drawing.Color]::FromArgb(63, 164, 255); $lancer.ForeColor = [System.Drawing.Color]::White
$fermer = New-Object System.Windows.Forms.Button
$fermer.Text = 'Fermer'; $fermer.SetBounds(434, 226, 80, 32); $fermer.Enabled = $false
$fermer.FlatStyle = 'Flat'; $fermer.ForeColor = [System.Drawing.Color]::White
$f.Controls.AddRange(@($lancer, $fermer))
$lancer.Add_Click({ Start-Process (Join-Path $Destination 'Onde.exe') -WorkingDirectory $Destination; $f.Close() })
$fermer.Add_Click({ $f.Close() })

$f.Add_Shown({
    $f.Refresh()
    try {
        Installer-Onde { param($t) $etat.Text = $t; $f.Refresh(); [System.Windows.Forms.Application]::DoEvents() }
        $barre.Style = 'Continuous'; $barre.Value = 100
        $etat.Text = if ($SansRaccourcis) { "Onde est installé dans $Destination." } else { "Onde est installé (menu Démarrer et Bureau).`nPour le retirer : Paramètres › Applications." }
        $lancer.Enabled = $true; $fermer.Enabled = $true; $lancer.Focus()
    } catch {
        $barre.Style = 'Continuous'; $barre.Value = 0
        $etat.ForeColor = [System.Drawing.Color]::FromArgb(232, 71, 63)
        $etat.Text = "Échec de l'installation : $($_.Exception.Message)"
        $fermer.Enabled = $true
    }
})
[void] $f.ShowDialog()
