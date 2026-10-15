# Dot-source to get clang + MSVC/Windows SDK headers and libs without vcvars64.bat
# (which fails on this machine's PATH). usage: . tools/host/env.ps1
$msvc = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC" | Sort-Object Name | Select-Object -Last 1
$kits = "C:\Program Files (x86)\Windows Kits\10"
$sdk = (Get-ChildItem "$kits\Include" | Sort-Object Name | Select-Object -Last 1).Name
$env:INCLUDE = "$($msvc.FullName)\include;$kits\Include\$sdk\ucrt;$kits\Include\$sdk\um;$kits\Include\$sdk\shared"
$env:LIB = "$($msvc.FullName)\lib\x64;$kits\Lib\$sdk\ucrt\x64;$kits\Lib\$sdk\um\x64"
$env:PATH = "C:\Program Files\LLVM\bin;" + $env:PATH
