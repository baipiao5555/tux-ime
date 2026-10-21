param(
    [string]$Prefix = "",
    [string]$BuildRoot = "",
    [switch]$SkipWin32
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Prefix) { $Prefix = Join-Path $root "prefix" }
if (-not $BuildRoot) { $BuildRoot = Join-Path $root "build" }

$msys = $env:MSYS2_ROOT
if (-not $msys) { $msys = "C:/msys64" }
$clang = Join-Path $msys "clang64"
$env:MSYS2_ROOT = $msys
$env:TUX_PREFIX = $Prefix
$env:PKG_CONFIG_PATH = Join-Path $clang "lib/pkgconfig"
$env:PATH = "$(Join-Path $Prefix 'bin');$(Join-Path $BuildRoot 'libime/bin');$(Join-Path $BuildRoot 'chinese/bin');$(Join-Path $clang 'bin');$env:PATH"

function Invoke-Step {
    param([string]$Name, [scriptblock]$Body)
    Write-Host "==> $Name" -ForegroundColor Cyan
    & $Body
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

function Apply-Patch {
    param([string]$Submodule, [string]$Patch)
    Push-Location (Join-Path $root $Submodule)
    try {
        git apply --reverse --check $Patch 2>$null
        if ($LASTEXITCODE -ne 0) {
            git apply $Patch
        }
    } finally {
        Pop-Location
    }
}

Apply-Patch "libime" (Join-Path $root "patches/libime-windows.patch")
Apply-Patch "fcitx5-chinese-addons" `
    (Join-Path $root "patches/fcitx5-chinese-addons-windows.patch")

$commonInstall = @(
    "-DCMAKE_BUILD_TYPE=Release",
    "-DARCH=AMD64",
    "-DCMAKE_INSTALL_PREFIX=$Prefix",
    "-DCMAKE_INSTALL_BINDIR=bin",
    "-DCMAKE_INSTALL_LIBDIR=lib",
    "-DCMAKE_INSTALL_DATADIR=data",
    "-DCMAKE_INSTALL_LOCALEDIR=data/locale"
)

Invoke-Step "fcitx5 core" {
    $arguments = @("-B", (Join-Path $BuildRoot "core"), "-G", "Ninja",
        "-S", $root) + $commonInstall
    & cmake @arguments
    cmake --build (Join-Path $BuildRoot "core")
    cmake --install (Join-Path $BuildRoot "core")
}

$depToolchain = Join-Path $root "cmake/msys2-with-prefix.toolchain.cmake"
Invoke-Step "libime" {
    $arguments = @("-B", (Join-Path $BuildRoot "libime"), "-G", "Ninja",
        "-S", (Join-Path $root "libime")) + $commonInstall + @(
        "-DCMAKE_TOOLCHAIN_FILE=$depToolchain", "-DENABLE_TEST=OFF")
    & cmake @arguments
    cmake --build (Join-Path $BuildRoot "libime")
    cmake --install (Join-Path $BuildRoot "libime")
}

Invoke-Step "fcitx5-chinese-addons" {
    $arguments = @("-B", (Join-Path $BuildRoot "chinese"), "-G", "Ninja",
        "-S", (Join-Path $root "fcitx5-chinese-addons")) + $commonInstall + @(
        "-DCMAKE_TOOLCHAIN_FILE=$depToolchain",
        "-DENABLE_OPENCC=OFF", "-DENABLE_GUI=OFF",
        "-DENABLE_BROWSER=OFF", "-DENABLE_CLOUDPINYIN=OFF",
        "-DENABLE_TEST=OFF", "-DENABLE_TOOLS=OFF")
    & cmake @arguments
    cmake --build (Join-Path $BuildRoot "chinese")
    cmake --install (Join-Path $BuildRoot "chinese")
}

if (-not $SkipWin32) {
    Invoke-Step "TSF frontend" {
        & cmake @("-B", (Join-Path $BuildRoot "win32"), "-G", "Ninja",
            "-S", (Join-Path $root "win32"), "-DCMAKE_BUILD_TYPE=Release",
            "-DTUX_USE_MSVC=ON")
        cmake --build (Join-Path $BuildRoot "win32")
    }
}

Write-Host "==> assemble dist/TuxIME-final" -ForegroundColor Cyan
$dist = Join-Path $root "dist/TuxIME-final"
Remove-Item $dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item (Join-Path $Prefix "bin") $dist -Recurse
Copy-Item (Join-Path $Prefix "lib") $dist -Recurse
Copy-Item (Join-Path $Prefix "data") $dist -Recurse

$win32Dir = Join-Path $BuildRoot "win32/dll"
if (-not (Test-Path (Join-Path $win32Dir "tuxime-x86_64.dll"))) {
    $win32Dir = Join-Path $root "build/local-win32-msvc/dll"
}
Copy-Item (Join-Path $win32Dir "tuxime-x86_64.dll") (Join-Path $dist "bin")
Copy-Item (Join-Path $win32Dir "penguin.ico") (Join-Path $dist "bin")
Copy-Item (Join-Path $root "README.md") $dist

# Remove development-only executables from the distribution.
Get-ChildItem (Join-Path $dist "bin") -File | Where-Object {
    $_.Name -match '^(Fcitx5|fcitx5-|libime_|tux-client-test|tux-ime-test|server-client-test|candidate-test)' -or
    $_.Extension -eq ".a"
} | Remove-Item -Force

# Copy the MSYS2 runtime DLLs that the server and addons need.
$binDir = Join-Path $dist "bin"
for ($round = 0; $round -lt 3; $round++) {
    $targets = Get-ChildItem $binDir -File | Where-Object {
        $_.Extension -in ".exe", ".dll"
    }
    $targets += Get-ChildItem (Join-Path $dist "lib/fcitx5") -File -Filter *.dll
    $deps = foreach ($target in $targets) {
        & ldd $target.FullName 2>$null |
            Select-String -Pattern '=> /c/msys64/clang64/' |
            ForEach-Object { ($_ -split '=> ')[1].Split(' ')[0] }
    }
    $deps = $deps | Sort-Object -Unique
    foreach ($dep in $deps) {
        $source = $dep -replace '^/c/msys64/', 'C:\msys64\' -replace '/', '\'
        if (Test-Path $source) {
            Copy-Item $source $binDir -Force -ErrorAction SilentlyContinue
        }
    }
}

Write-Host "Built $dist" -ForegroundColor Green
