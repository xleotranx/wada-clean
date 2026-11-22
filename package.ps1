param(
    [switch]$LocalWada,
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $root "build.ps1") -LocalWada:$LocalWada
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$candidates = @(
    (Join-Path $root ".tools\w64devkit\w64devkit\bin"),
    (Join-Path $root "..\c-desktop\.tools\w64devkit\w64devkit\bin"),
    (Join-Path $root "..\wada-c\.tools\w64devkit\w64devkit\bin"),
    "$env:WADA_W64DEVKIT"
)
$stripExe = $null
foreach ($c in $candidates) {
    if ($c -and (Test-Path (Join-Path $c "strip.exe"))) {
        $stripExe = Join-Path $c "strip.exe"
        break
    }
}

$distDir = Join-Path $root "dist"
$stageDir = Join-Path $distDir "stage"
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

$exeSrc = Join-Path $root "build\wada_clean.exe"
$exeDst = Join-Path $stageDir "wada_clean.exe"
Copy-Item $exeSrc $exeDst -Force

if ($stripExe) {
    & $stripExe --strip-unneeded $exeDst
    if ($LASTEXITCODE -ne 0) { throw "strip failed" }
}

Copy-Item (Join-Path $root "LICENSE") (Join-Path $stageDir "LICENSE") -Force
Copy-Item (Join-Path $root "README.md") (Join-Path $stageDir "README.md") -Force

$zipName = "wada-clean-v$Version-win64.zip"
$zipPath = Join-Path $distDir $zipName
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath
$hash = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLower()
"$hash  $zipName" | Set-Content -Path "$zipPath.sha256" -Encoding ASCII

$exeMB = [math]::Round((Get-Item $exeDst).Length / 1MB, 2)
$zipMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Remove-Item -Recurse -Force $stageDir

Write-Host ""
Write-Host "package ok:"
Write-Host "  stripped binary: $exeMB MB"
Write-Host "  archive:         dist\$zipName ($zipMB MB)"
Write-Host "  sha256:          $hash"
