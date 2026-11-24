# Compila em Release e prepara os ficheiros de uma release em dist/:
#   OpenASCII-<versão>-setup.exe      instalador (se o Inno Setup 6 estiver instalado)
#   OpenASCII-<versão>-win64.zip      versão portátil (descompactar e correr)
#   SHA256SUMS.txt                     somas de verificação dos dois
# Uso: pwsh scripts/package.ps1 [-SkipBuild] [-RequireInstaller]
param(
    [switch]$SkipBuild,        # usar o build\msvc\Release que já existe (ex.: no CI, depois dos testes)
    [switch]$RequireInstaller  # falhar se o Inno Setup não estiver disponível (CI)
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Config Release
}

# Uma só fonte para a versão: project(OpenASCII VERSION x.y.z) no CMakeLists.txt.
$cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(OpenASCII\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'Versão não encontrada no CMakeLists.txt' }
$version = $Matches[1]

$exePath = Join-Path $root 'build\msvc\Release\openascii.exe'
if (-not (Test-Path $exePath)) { throw "Falta $exePath (compila primeiro ou tira o -SkipBuild)" }

$name = "OpenASCII-$version-win64"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $exePath $stage
Copy-Item (Join-Path $root 'wallpapers') (Join-Path $stage 'wallpapers') -Recurse
Copy-Item (Join-Path $root 'docs\CREATING_WALLPAPERS.md') $stage
Copy-Item (Join-Path $root 'README.md'), (Join-Path $root 'LICENSE'), (Join-Path $root 'CHANGELOG.md') $stage

# 1) Versão portátil.
$zip = Join-Path $dist "$name.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
$artifacts = @($zip)

# 2) Instalador (Inno Setup 6): por utilizador, sem pedir administrador.
$iscc = @(
    (Get-Command iscc -ErrorAction SilentlyContinue).Source,
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($iscc) {
    $setup = Join-Path $dist "OpenASCII-$version-setup.exe"
    Remove-Item $setup -Force -ErrorAction SilentlyContinue
    & $iscc /Q "/DAppVersion=$version" "/DSourceDir=$stage" "/O$dist" (Join-Path $root 'installer\OpenASCII.iss')
    if ($LASTEXITCODE) { throw 'O Inno Setup falhou a compilar o instalador' }
    $artifacts += $setup
} elseif ($RequireInstaller) {
    throw 'Inno Setup 6 (ISCC.exe) não encontrado'
} else {
    Write-Warning 'Inno Setup 6 não encontrado: só a versão portátil foi criada (winget install JRSoftware.InnoSetup)'
}

# 3) Somas de verificação (formato do sha256sum: "<hash>  <ficheiro>").
$sums = foreach ($a in $artifacts) {
    "{0}  {1}" -f (Get-FileHash $a -Algorithm SHA256).Hash.ToLower(), (Split-Path $a -Leaf)
}
$sums | Set-Content (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii

$exe = Get-Item (Join-Path $stage 'openascii.exe')
Write-Host ("OpenASCII {0} (exe: {1:N0} KB)" -f $version, ($exe.Length / 1KB))
foreach ($a in $artifacts) { Write-Host ("  {0}  ({1:N1} MB)" -f (Split-Path $a -Leaf), ((Get-Item $a).Length / 1MB)) }
Write-Host "  SHA256SUMS.txt"
