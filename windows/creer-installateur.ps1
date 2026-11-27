# Onde — fabrique l'installateur Windows à partir de la dernière compilation (build-windows).
#   build-windows\installateur\Onde-Setup-<version>.exe      installateur d'Onde (étapes, désinstalleur, fichiers .onde ;
#                                                             --silencieux [--dossier D] pour une installation sans fenêtre)
#   build-windows\installateur\Onde-<version>-portable.zip   même contenu, à décompresser n'importe où
# Options : -SansModeles (sans les modèles du séparateur, ≈ 320 Mo de moins), -Version x.y
param([switch] $SansModeles, [string] $Version = '')
$ErrorActionPreference = 'Stop'
$projet = Split-Path -Parent $PSScriptRoot
$rel = Join-Path $projet 'build-windows\app\Onde_artefacts\Release'
$sortie = Join-Path $projet 'build-windows\installateur'
if (-not (Test-Path (Join-Path $rel 'Onde.exe'))) { throw "Onde.exe introuvable : compilez d'abord (windows\Installer-et-lancer-Onde.bat)." }
# Programme d'installation (petit, compilé à part).
cmake --build (Join-Path $projet 'build-windows') --config Release --target onde-setup | Out-Null
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
# La version la PLUS RÉCENTE : le programme est compilé avec le dernier compilateur ; un runtime plus ancien posé
# à côté de lui l'empêcherait de démarrer (Visual Studio garde souvent plusieurs versions de redistribuables).
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory -ErrorAction SilentlyContinue |
       Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version] $_.Name } -Descending |
       ForEach-Object { Get-ChildItem $_.FullName -Recurse -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue |
                        Where-Object { $_.FullName -match '\\x64\\' -and $_.FullName -notmatch 'onecore|Debug' } } |
       Select-Object -First 1
if (-not $crt) { throw 'Runtime Visual C++ (Microsoft.VC14x.CRT, x64) introuvable dans Visual Studio.' }
Copy-Item (Join-Path $crt.FullName '*.dll') $app
Write-Host "Runtime : $($crt.FullName)"

# 2. Archive et installateur.
# L'installateur est un programme d'Onde (onde-setup.exe : étapes, désinstalleur, fichiers .onde) ; l'archive de
# l'application est ajoutée à sa fin, suivie de sa position (8 octets) et de la signature « ONDEPAK1 ».
Set-Content (Join-Path $app 'version.txt') $Version
$zip = Join-Path $paquet 'onde.zip'
Write-Host 'Compression…'
Compress-Archive -Path (Join-Path $app '*') -DestinationPath $zip -CompressionLevel Optimal

$stub = Join-Path $projet 'build-windows\app\onde-setup_artefacts\Release\onde-setup.exe'
if (-not (Test-Path $stub)) { throw "onde-setup.exe introuvable : cmake --build build-windows --config Release --target onde-setup" }
$setup = Join-Path $sortie "Onde-Setup-$Version.exe"
$portable = Join-Path $sortie "Onde-$Version-portable.zip"
Remove-Item $setup, $portable -ErrorAction SilentlyContinue
Copy-Item $zip $portable
Write-Host 'Fabrication de l''installateur…'
$out = [IO.File]::Open($setup, [IO.FileMode]::Create)
try {
    $bytes = [IO.File]::ReadAllBytes($stub)
    $out.Write($bytes, 0, $bytes.Length)
    $offset = [long] $out.Position
    $in = [IO.File]::OpenRead($zip)
    try { $in.CopyTo($out) } finally { $in.Dispose() }
    $off = [BitConverter]::GetBytes($offset)          # petit-boutiste, comme InputStream::readInt64 de JUCE
    $out.Write($off, 0, 8)
    $magic = [Text.Encoding]::ASCII.GetBytes('ONDEPAK1')
    $out.Write($magic, 0, 8)
} finally { $out.Dispose() }
"{0}  {1:N0} Mo" -f $setup, ((Get-Item $setup).Length / 1MB)
"{0}  {1:N0} Mo" -f $portable, ((Get-Item $portable).Length / 1MB)
