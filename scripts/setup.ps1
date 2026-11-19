# Instala as ferramentas de build que faltam (CMake, Ninja, vcpkg) numa pasta do
# utilizador, sem precisar de admin nem de mexer no PATH do sistema.
# Uso: pwsh scripts/setup.ps1 [-ToolsDir <pasta>]
param(
    [string]$ToolsDir = (Join-Path $env:USERPROFILE 'dev-tools')
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$cmakeVersion = '3.30.5'
$ninjaVersion = '1.12.1'

New-Item -ItemType Directory -Force $ToolsDir | Out-Null

$cmakeDir = Join-Path $ToolsDir "cmake-$cmakeVersion-windows-x86_64"
if (-not (Test-Path "$cmakeDir\bin\cmake.exe")) {
    Write-Host "A transferir CMake $cmakeVersion..."
    $zip = Join-Path $ToolsDir 'cmake.zip'
    Invoke-WebRequest "https://github.com/Kitware/CMake/releases/download/v$cmakeVersion/cmake-$cmakeVersion-windows-x86_64.zip" -OutFile $zip
    Expand-Archive $zip -DestinationPath $ToolsDir -Force
    Remove-Item $zip
}

$ninjaDir = Join-Path $ToolsDir 'ninja'
if (-not (Test-Path "$ninjaDir\ninja.exe")) {
    Write-Host "A transferir Ninja $ninjaVersion..."
    $zip = Join-Path $ToolsDir 'ninja.zip'
    Invoke-WebRequest "https://github.com/ninja-build/ninja/releases/download/v$ninjaVersion/ninja-win.zip" -OutFile $zip
    New-Item -ItemType Directory -Force $ninjaDir | Out-Null
    Expand-Archive $zip -DestinationPath $ninjaDir -Force
    Remove-Item $zip
}

$vcpkgDir = Join-Path $ToolsDir 'vcpkg'
if (-not (Test-Path "$vcpkgDir\vcpkg.exe")) {
    Write-Host 'A clonar e preparar o vcpkg...'
    if (-not (Test-Path $vcpkgDir)) {
        git clone https://github.com/microsoft/vcpkg.git $vcpkgDir
    }
    # O bootstrap do vcpkg chama "powershell.exe" (Windows PowerShell 5.1).
    $env:PATH = "$env:SystemRoot\System32\WindowsPowerShell\v1.0;$env:PATH"
    & "$vcpkgDir\bootstrap-vcpkg.bat" -disableMetrics
}

Write-Host "Ferramentas prontas em $ToolsDir"
Write-Host 'Para compilar: pwsh scripts/build.ps1'
