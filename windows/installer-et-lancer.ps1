# Onde — installe les outils, compile et lance le prototype sous Windows 10/11.
# Lancement : double-clic sur « Installer-et-lancer-Onde.bat » (dans le même dossier).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$projet = Split-Path -Parent $PSScriptRoot          # racine du dépôt
# Dépendances dans deps\ du dépôt (ignoré par git) ; ancienne disposition ..\deps reprise si elle existe déjà.
$deps   = Join-Path $projet 'deps'
$ancien = Join-Path (Split-Path -Parent $projet) 'deps'
if (-not (Test-Path (Join-Path $deps 'tracktion_engine')) -and (Test-Path (Join-Path $ancien 'tracktion_engine\modules\juce\CMakeLists.txt'))) { $deps = $ancien }
$te     = Join-Path $deps 'tracktion_engine'
$build  = Join-Path $projet 'build-windows'

# Versions exactes avec lesquelles Onde est développé et testé.
$teCommit     = 'a5e44996f5f08dfa6af5f5d02d900fb3cfa37422'   # Tracktion Engine (JUCE suit en sous-module)
$demucsCommit = '4edae497a114de1ccb17ac9b4a2b8fc0144714d7'   # demucs.cpp : seul Eigen (vendor/eigen) est utilisé
$modeles      = 'drums','bass','other','vocals'

# Clone superficiel d'un commit précis (GitHub accepte de récupérer un commit par son identifiant).
function Cloner-Commit($url, $dossier, $commit) {
    if (Test-Path (Join-Path $dossier '.git')) { return }
    if (Test-Path $dossier) { Remove-Item -Recurse -Force $dossier }   # reste d'un essai interrompu
    New-Item -ItemType Directory -Force -Path $dossier | Out-Null
    git -C $dossier init -q
    git -C $dossier remote add origin $url
    git -C $dossier -c core.longpaths=true fetch -q --depth 1 origin $commit
    if ($LASTEXITCODE -ne 0) { throw "Le téléchargement de $url a échoué (connexion ?)." }
    git -C $dossier -c core.longpaths=true checkout -q FETCH_HEAD
}

function Etape($texte) { Write-Host "`n== $texte" -ForegroundColor Yellow }
function Rafraichir-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}
function Installer($id, $nom) {
    Write-Host "Installation de $nom (winget)..."
    winget install --id $id -e --source winget --accept-package-agreements --accept-source-agreements --silent
    Rafraichir-Path
}

if ($projet.Length -gt 60) {
    Write-Host "Attention : le dossier du projet a un chemin long ($($projet.Length) caractères)." -ForegroundColor Red
    Write-Host "Windows limite les chemins à 260 caractères : déplacez le dossier, par exemple dans C:\onde\onde-proto." -ForegroundColor Red
}

Etape '1/6 Vérification des outils'
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget est introuvable. Installez « App Installer » depuis le Microsoft Store, puis relancez."
}
if (-not (Get-Command git -ErrorAction SilentlyContinue))   { Installer 'Git.Git' 'Git' }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Installer 'Kitware.CMake' 'CMake' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) {
    Write-Host 'Installation des outils de compilation C++ de Microsoft (environ 3 Go, 10 à 20 minutes)...'
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements `
        --override '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "Les outils C++ de Visual Studio ne sont pas installés. Relancez ce script." }
}
Write-Host "Outils C++ : $vs"

Etape '2/6 Téléchargement de Tracktion Engine et JUCE (une seule fois)'
$juceOk = { Test-Path (Join-Path $te 'modules\juce\CMakeLists.txt') }
Cloner-Commit 'https://github.com/Tracktion/tracktion_engine.git' $te $teCommit
if (-not (& $juceOk)) {
    # Tracktion déclare JUCE en SSH (git@github.com:…), ce qui exige une clé SSH GitHub : on force HTTPS.
    git -C $te config submodule.modules/juce.url https://github.com/juce-framework/JUCE.git
    git -C $te -c core.longpaths=true -c url.https://github.com/.insteadOf=git@github.com: submodule update --init --depth 1 modules/juce
    if (-not (& $juceOk)) {
        Write-Host 'Nouvel essai de téléchargement de JUCE (historique complet, plus long)...'
        git -C $te -c core.longpaths=true -c url.https://github.com/.insteadOf=git@github.com: submodule update --init --force modules/juce
    }
    if (-not (& $juceOk)) { throw "JUCE n'a pas pu être téléchargé dans $te\modules\juce. Copiez les messages ci-dessus." }
}
Write-Host 'Tracktion Engine et JUCE sont prêts.'

# Séparateur de sources : Eigen (via demucs.cpp) et les 4 modèles Demucs v4 fine-tuned (≈ 340 Mo, licence MIT).
# Facultatif : sans eux, Onde se compile et fonctionne, seule la séparation est indisponible.
$dm = Join-Path $deps 'demucs.cpp'
try {
    Cloner-Commit 'https://github.com/sevagh/demucs.cpp.git' $dm $demucsCommit
    if (-not (Test-Path (Join-Path $dm 'vendor\eigen\Eigen\Dense'))) {
        git -C $dm -c core.longpaths=true submodule update --init --depth 1 vendor/eigen
    }
    $md = Join-Path $deps 'demucs-models'
    New-Item -ItemType Directory -Force -Path $md | Out-Null
    foreach ($n in $modeles) {
        $f = "ggml-model-htdemucs_ft_$n-4s-f16.bin"
        $dest = Join-Path $md $f
        if (-not (Test-Path $dest) -or (Get-Item $dest).Length -lt 50MB) {
            Write-Host "Modèle $n (80 Mo)..."
            curl.exe -L --fail --retry 3 -o $dest ("https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/" + $f + "?download=true")
        }
    }
    Write-Host 'Séparateur de sources prêt.'
} catch {
    Write-Host "Séparateur de sources non installé ($($_.Exception.Message)) : le reste d'Onde fonctionne." -ForegroundColor Red
}

Etape '3/6 Configuration'
cmake -S $projet -B $build -G 'Visual Studio 17 2022' -A x64 "-DTRACKTION_ENGINE_DIR=$te"
if ($LASTEXITCODE -ne 0) { throw 'La configuration CMake a échoué (voir les messages ci-dessus).' }

Etape '4/6 Compilation (15 à 30 minutes la première fois)'
cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'La compilation a échoué (voir les messages ci-dessus).' }

Etape '5/6 Tests (isolation, puis vrai VST3 qui plante)'
$core = Join-Path $build 'core\Release'
& (Join-Path $core 'onde-sandbox-test.exe') (Join-Path $core 'onde-plugin-worker.exe')
$appDir = Join-Path $build 'app\Onde_artefacts\Release'
& (Join-Path $build 'app\Release\onde-vst3-test.exe') (Join-Path $appDir 'onde-plugin-host.exe') `
    (Join-Path $appDir 'plugins-de-test\Onde Test Synth.vst3') (Join-Path $appDir 'plugins-de-test\Onde Test Crash.vst3')

Etape '6/6 Lancement d''Onde'
$exe = Join-Path $build 'app\Onde_artefacts\Release\Onde.exe'
Write-Host "Application : $exe"
Write-Host 'Astuce : épinglez Onde.exe à la barre des tâches pour les prochaines fois.'
Start-Process $exe
