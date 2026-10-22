param(
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$tip = "0804:{8D7031FD-4D2D-4CCE-BB41-05F54D7AC612}{76AE52E2-7F68-4A6D-81D0-A48D4AC73E36}"
$language = "zh-Hans-CN"
$profileKey = "HKCU:\Control Panel\International\User Profile"

function Restart-TextServices {
    Stop-Process -Name ctfmon -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    Start-Process "$env:WINDIR\System32\ctfmon.exe" -ErrorAction SilentlyContinue
}

try {
    $list = Get-WinUserLanguageList
    $entry = $list | Where-Object { $_.LanguageTag -eq $language } | Select-Object -First 1

    if ($Remove) {
        if ($entry -and $entry.InputMethodTips.Contains($tip)) {
            $entry.InputMethodTips.Remove($tip)
            Set-WinUserLanguageList $list -Force
        }

        $override = (Get-ItemProperty -Path $profileKey -Name InputMethodOverride -ErrorAction SilentlyContinue).InputMethodOverride
        if ($override -eq $tip) {
            Remove-ItemProperty -Path $profileKey -Name InputMethodOverride -ErrorAction SilentlyContinue
        }
    } else {
        if (-not $entry) {
            $entry = New-WinUserLanguageList -LanguageList $language | Select-Object -First 1
            $list.Add($entry)
        }

        if ($entry.InputMethodTips.Contains($tip)) {
            $entry.InputMethodTips.Remove($tip)
        }
        # Put Tux first so it becomes the primary Chinese input method.
        $entry.InputMethodTips.Insert(0, $tip)
        Set-WinUserLanguageList $list -Force

        if (-not (Test-Path $profileKey)) {
            New-Item -Path $profileKey -Force | Out-Null
        }
        Set-ItemProperty -Path $profileKey -Name InputMethodOverride -Value $tip
    }

    Restart-TextServices
    exit 0
} catch {
    # The installer keeps running even if Windows refuses the language-list
    # update; the TIP is still registered and can be added manually.
    try {
        $logDir = Join-Path $env:ProgramData "TuxIME"
        New-Item -ItemType Directory -Path $logDir -Force | Out-Null
        "$(Get-Date -Format s) $($_.Exception.Message)" |
            Out-File -FilePath (Join-Path $logDir "setup-language.log") -Append -Encoding utf8
    } catch {
    }
    exit 1
}
