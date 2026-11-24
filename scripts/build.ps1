# Configura, compila e corre os testes.
# Uso: pwsh scripts/build.ps1 [-Config Debug|Release] [-NoTests]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [switch]$NoTests
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
. (Join-Path $PSScriptRoot 'env.ps1')

Push-Location $root
try {
    cmake --preset msvc
    if ($LASTEXITCODE) { throw 'cmake configure falhou' }
    cmake --build --preset "msvc-$($Config.ToLower())"
    if ($LASTEXITCODE) { throw 'build falhou' }
    if (-not $NoTests) {
        ctest --preset "msvc-$($Config.ToLower())"
        if ($LASTEXITCODE) { throw 'testes falharam' }
    }
    Write-Host "OK: build\msvc\$Config\openascii.exe"
} finally {
    Pop-Location
}
