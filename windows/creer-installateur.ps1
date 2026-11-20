# Onde — fabrique l'installateur Windows à partir de la dernière compilation (build-windows).
#   build-windows\installateur\Onde-Setup-<version>.exe      installateur (IExpress, intégré à Windows)
#   build-windows\installateur\Onde-<version>-portable.zip   même contenu, à décompresser n'importe où
# Options : -SansModeles (sans les modèles du séparateur, ≈ 320 Mo de moins), -Version x.y
param([switch] $SansModeles, [string] $Version = '')
$ErrorActionPreference = 'Stop'
$projet = Split-Path -Parent $PSScriptRoot
$rel = Join-Path $projet 'build-windows\app\Onde_artefacts\Release'
$sortie = Join-Path $projet 'build-windows\installateur'
if (-not (Test-Path (Join-Path $rel 'Onde.exe'))) { throw "Onde.exe introuvable : compilez d'abord (windows\Installer-et-lancer-Onde.bat)." }
if (-not $Version) {
    $m = Select-String -Path (Join-Path $projet 'app\Source\AboutPanel.h') -Pattern 'version = "([^"]+)"' | Select-Object -First 1
    $Version = if ($m) { $m.Matches[0].Groups[1].Value } else { '0.0' }
}
Write-Host "Onde $Version"

# 1. Contenu de l'application.
$app = Join-Path $sortie 'app'
$paquet = Join-Path $sortie 'paquet'
Remove-Item $app, $paquet -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $app, $paquet | Out-Null
foreach ($f in 'Onde.exe', 'onde-plugin-host.exe', 'onde-stems.exe') {
    if (Test-Path (Join-Path $rel $f)) { Copy-Item (Join-Path $rel $f) $app }
}
# Plugins de démonstration (le projet « Nuits du Nord » utilise « Onde Test Synth »).
if (Test-Path (Join-Path $rel 'plugins-de-test')) { Copy-Item (Join-Path $rel 'plugins-de-test') $app -Recurse }
if (-not $SansModeles -and (Test-Path (Join-Path $rel 'modeles-demucs'))) { Copy-Item (Join-Path $rel 'modeles-demucs') $app -Recurse }

# Runtime Visual C++ à côté du programme (déploiement « app-local ») : rien à installer en plus.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = if (Test-Path $vswhere) { & $vswhere -latest -products * -property installationPath } else { $null }
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Recurse -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -match '\\x64\\' -and $_.FullName -notmatch 'onecore|Debug' } | Select-Object -First 1
if (-not $crt) { throw 'Runtime Visual C++ (Microsoft.VC14x.CRT, x64) introuvable dans Visual Studio.' }
Copy-Item (Join-Path $crt.FullName '*.dll') $app
Write-Host "Runtime : $($crt.FullName)"

# 2. Archive et installateur.
$zip = Join-Path $paquet 'onde.zip'
Write-Host 'Compression…'
Compress-Archive -Path (Join-Path $app '*') -DestinationPath $zip -CompressionLevel Optimal
# Scripts en UTF-8 avec BOM : sinon Windows PowerShell 5.1 les lit en ANSI et les accents sont abîmés.
foreach ($script in 'installer.ps1', 'desinstaller.ps1') {
    $texte = [IO.File]::ReadAllText((Join-Path $PSScriptRoot "installateur\$script"), [Text.Encoding]::UTF8)
    [IO.File]::WriteAllText((Join-Path $paquet $script), $texte, (New-Object Text.UTF8Encoding $true))
}
Copy-Item (Join-Path $projet 'app\Resources\logo-192.png') (Join-Path $paquet 'logo.png')
Set-Content (Join-Path $paquet 'version.txt') $Version

$setup = Join-Path $sortie "Onde-Setup-$Version.exe"
$portable = Join-Path $sortie "Onde-$Version-portable.zip"
Remove-Item $setup, $portable -ErrorAction SilentlyContinue
Copy-Item $zip $portable
$fichiers = 'installer.ps1', 'desinstaller.ps1', 'onde.zip', 'logo.png', 'version.txt'
$sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$setup
FriendlyName=Onde $Version
AppLaunched=cmd /c powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File installer.ps1
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
$(($fichiers | ForEach-Object -Begin { $i = 0 } -Process { "FILE$i=`"$_`""; $i++ }) -join "`r`n")
[SourceFiles]
SourceFiles0=$paquet\
[SourceFiles0]
$(($fichiers | ForEach-Object -Begin { $i = 0 } -Process { "%FILE$i%="; $i++ }) -join "`r`n")
"@
$sedFile = Join-Path $sortie 'onde.sed'
Set-Content $sedFile $sed -Encoding Ascii
Write-Host 'Fabrication de l''installateur (IExpress)…'
# IExpress refuse un chemin entre guillemets : le fichier SED est placé dans un dossier sans espace.
if ($sedFile.Contains(' ')) {
    $court = Join-Path $env:TEMP 'onde-installateur.sed'
    Copy-Item $sedFile $court -Force
    $sedFile = $court
}
$p = Start-Process (Join-Path $env:SystemRoot 'System32\iexpress.exe') -ArgumentList "/N /Q $sedFile" -Wait -PassThru
# IExpress peut rendre la main avant la fin de la compression (makecab) : on attend le fichier final.
$limite = (Get-Date).AddMinutes(15)
while ((Get-Date) -lt $limite -and ((Get-Process makecab, iexpress -ErrorAction SilentlyContinue) -or -not (Test-Path $setup))) {
    if (-not (Get-Process makecab, iexpress -ErrorAction SilentlyContinue) -and -not (Test-Path $setup)) { break }
    Start-Sleep 1
}
if (-not (Test-Path $setup)) { throw "IExpress n'a pas produit l'installateur (code $($p.ExitCode))." }
"{0}  {1:N0} Mo" -f $setup, ((Get-Item $setup).Length / 1MB)
"{0}  {1:N0} Mo" -f $portable, ((Get-Item $portable).Length / 1MB)
