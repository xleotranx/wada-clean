param(
    [switch]$LocalWada,
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel", "Debug")]
    [string]$BuildType = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$candidates = @(
    (Join-Path $root ".tools\w64devkit\w64devkit\bin"),
    (Join-Path $root "..\c-desktop\.tools\w64devkit\w64devkit\bin"),
    (Join-Path $root "..\wada-c\.tools\w64devkit\w64devkit\bin"),
    "$env:WADA_W64DEVKIT"
)

$bin = $null
foreach ($c in $candidates) {
    if ($c -and (Test-Path (Join-Path $c "gcc.exe"))) {
        $bin = $c
        break
    }
}
if (-not $bin) {
    throw "w64devkit not found. Put it at .tools\w64devkit\w64devkit or set WADA_W64DEVKIT."
}

$env:PATH = "$bin;$env:PATH"
Write-Host "toolchain: $bin"

$cmakeExtraArgs = @()
if ($LocalWada) {
    $localPath = (Resolve-Path (Join-Path $root "..\wada-c")).Path
    Write-Host "using local wada-c: $localPath"
    $cmakeExtraArgs += "-DWADA_C_DIR=$localPath"
} else {
    $subCmake = Join-Path $root "vendor\wada-c\CMakeLists.txt"
    if (-not (Test-Path $subCmake)) {
        Write-Host "initializing vendor/wada-c submodule..."
        Push-Location $root
        try {
            git submodule update --init --recursive
            if ($LASTEXITCODE -ne 0) { throw "git submodule update failed" }
        } finally {
            Pop-Location
        }
    }
}

$buildDir = Join-Path $root "build"
Write-Host "build type: $BuildType"

cmake -S $root -B $buildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    -DCMAKE_C_COMPILER="$bin\gcc.exe" `
    @cmakeExtraArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$tests = Get-ChildItem $buildDir -Filter "*_tests.exe" | Sort-Object Name
foreach ($t in $tests) {
    Write-Host ""
    Write-Host "--- $($t.Name)"
    & $t.FullName
    if ($LASTEXITCODE -ne 0) { throw "$($t.Name) failed" }
}

Write-Host ""
Write-Host "build ok: build\wada_clean.exe"
