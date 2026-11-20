# Compila em Release e cria dist/AsciiPaper-<versão>-win64.zip pronto para uma release.
# Uso: pwsh scripts/package.ps1
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

& (Join-Path $PSScriptRoot 'build.ps1') -Config Release

$version = (Get-Content (Join-Path $root 'vcpkg.json') -Raw | ConvertFrom-Json).version
$name = "AsciiPaper-$version-win64"
$stage = Join-Path $root "dist\$name"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $root 'build\msvc\Release\asciipaper.exe') $stage
Copy-Item (Join-Path $root 'wallpapers') (Join-Path $stage 'wallpapers') -Recurse
Copy-Item (Join-Path $root 'docs\CREATING_WALLPAPERS.md') $stage
Copy-Item (Join-Path $root 'README.md'), (Join-Path $root 'LICENSE'), (Join-Path $root 'CHANGELOG.md') $stage

$zip = Join-Path $root "dist\$name.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Set-Content (Join-Path $root "dist\$name.zip.sha256") -NoNewline

$exe = Get-Item (Join-Path $stage 'asciipaper.exe')
Write-Host ("Pacote: {0} (exe: {1:N0} KB)" -f $zip, ($exe.Length / 1KB))
Write-Host "SHA-256: $hash"
