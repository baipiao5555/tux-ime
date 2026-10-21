$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$server = Join-Path $root "prefix/bin/tuxime-server.exe"
$appData = $env:APPDATA
$pinyin = Join-Path $appData "Fcitx5/config/fcitx5/conf/pinyin.conf"
$profile = Join-Path $appData "Fcitx5/config/fcitx5/profile"
$sync = Join-Path $appData "TuxIME/sync.ini"

$backup = Join-Path $env:TEMP "tuxime-settings-backup"
Remove-Item $backup -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $backup | Out-Null
foreach ($file in @($pinyin, $profile, $sync)) {
    if (Test-Path $file) {
        Copy-Item $file (Join-Path $backup (Split-Path $file -Leaf)) -Force
    }
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class W {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
}
"@

function Set-Combo([IntPtr]$parent, [int]$id, [int]$index) {
    $control = [W]::GetDlgItem($parent, $id)
    [void][W]::SendMessage($control, 0x014E, [IntPtr]$index, [IntPtr]::Zero)
}
function Set-Check([IntPtr]$parent, [int]$id, [bool]$checked) {
    $control = [W]::GetDlgItem($parent, $id)
    [void][W]::SendMessage($control, 0x00F1, [IntPtr]($(if ($checked) { 1 } else { 0 })), [IntPtr]::Zero)
}
function Set-Text([IntPtr]$parent, [int]$id, [string]$text) {
    $control = [W]::GetDlgItem($parent, $id)
    [void][W]::SendMessage($control, 0x000C, [IntPtr]::Zero, $text)
}

Get-Process tuxime-server -ErrorAction SilentlyContinue | Stop-Process -Force
$process = Start-Process $server -ArgumentList "--settings" -PassThru
try {
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) { break }
    }
    $handle = $process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) { throw "settings window not found" }

    Set-Combo $handle 2001 0
    Set-Combo $handle 2002 2
    Set-Check $handle 2010 $true
    Set-Check $handle 2011 $false
    Set-Check $handle 2020 $true
    Set-Text $handle 2021 "https://example.com"
    Set-Text $handle 2022 "tester"
    Set-Text $handle 2023 "secret"

    $apply = [W]::GetDlgItem($handle, 2040)
    [void][W]::SendMessage($handle, 0x0111, [IntPtr]2040, $apply)
    Start-Sleep -Seconds 2
} finally {
    Get-Process tuxime-server -ErrorAction SilentlyContinue | Stop-Process -Force
}

Write-Output "PINYIN:"
Get-Content $pinyin | Select-String -Pattern "PageSize|Z_ZH|DefaultIM" | Select-Object -First 5
Write-Output "PROFILE:"
Get-Content $profile | Select-String -Pattern "DefaultIM" | Select-Object -First 3
Write-Output "SYNC:"
if (Test-Path $sync) { Get-Content $sync }

foreach ($file in @($pinyin, $profile, $sync)) {
    $name = Split-Path $file -Leaf
    $saved = Join-Path $backup $name
    if (Test-Path $saved) {
        Copy-Item $saved $file -Force
    } elseif (Test-Path $file) {
        Remove-Item $file -Force
    }
}
