# Prepara a sessão atual do PowerShell para compilar: PATH com CMake/Ninja e VCPKG_ROOT.
# Uso: . scripts/env.ps1   (com o ponto, para afetar a sessão atual)
param(
    [string]$ToolsDir = (Join-Path $env:USERPROFILE 'dev-tools')
)

$cmake = Get-ChildItem $ToolsDir -Directory -Filter 'cmake-*' -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1
if ($cmake) { $env:PATH = "$($cmake.FullName)\bin;$env:PATH" }
if (Test-Path "$ToolsDir\ninja\ninja.exe") { $env:PATH = "$ToolsDir\ninja;$env:PATH" }

if (-not $env:VCPKG_ROOT -and (Test-Path "$ToolsDir\vcpkg\vcpkg.exe")) {
    $env:VCPKG_ROOT = "$ToolsDir\vcpkg"
}
# O vcpkg usa o Windows PowerShell em alguns ports.
$env:PATH = "$env:SystemRoot\System32\WindowsPowerShell\v1.0;$env:PATH"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Warning 'CMake não encontrado. Corre primeiro: pwsh scripts/setup.ps1'
}
if (-not $env:VCPKG_ROOT) {
    Write-Warning 'VCPKG_ROOT não definido. Corre primeiro: pwsh scripts/setup.ps1'
}
