param([string]$Output = 'build/mtp/switch2_wpd.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot)
try {
    . "$PSScriptRoot/host/env.ps1"
    $winrt = "${env:ProgramFiles(x86)}/Windows Kits/10/Include/$sdk/winrt"
    New-Item -ItemType Directory -Force (Split-Path $Output) | Out-Null
    & clang++.exe -std=c++17 -DUNICODE -D_UNICODE -isystem $winrt tools/switch2_wpd.cpp -o $Output -lole32 -lPortableDeviceGuids -lpropsys
    if ($LASTEXITCODE -ne 0) { throw 'WPD helper compilation failed' }
} finally { Pop-Location }
