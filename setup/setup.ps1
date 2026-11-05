<#
Sonic Riders native port: setup tool (Windows).

Builds the port from YOUR OWN disc image, on your PC. Nothing from the game is downloaded or
shipped: the tool checks your image, recompiles its code locally, builds the Switch homebrew
(.nro), a PC build and/or an Android APK, and copies the Switch build to your SD card.

  SonicRidersSetup.bat                 (double-click: asks for everything)
  powershell -ExecutionPolicy Bypass -File setup\setup.ps1 -Iso <image> -Target switch -Sd E:\

Parameters (all optional; asked for when missing):
  -Iso PATH          your Sonic Riders (USA) disc image, GXEE8P revision 0 (Redump, 1.4 GB .iso)
  -Target T          switch | pc | both (Switch + PC) | android | all
  -Abi A             Android: arm64-v8a (phones, default) | x86_64 (the emulator) | both
  -Install yes|no    Android: install the APK on the phone connected by USB (adb), if there is one
  -AcceptAndroidSdkLicense   Android: you accept the Android SDK License (https://developer.android.com/studio/terms);
                     without it the setup asks (and -Yes stops)
  -Sd PATH           the SD card (drive or folder) to install to; files go to <Sd>\switch\sonicriders\
  -NoSd              build only, do not copy to an SD card
  -CopyIso yes|no    also copy the disc image to the SD card (the Switch build needs it there once)
  -DesktopShortcut yes|no   PC: also put a "Sonic Riders" shortcut on the desktop
  -Icon PATH         PC / Android: your own program icon (.ico; Android also .png) instead of the game's save icon
  -ToolsDir PATH     where the downloaded toolchain is kept (default %LOCALAPPDATA%\SonicRidersSetup\tools)
  -Yes               no questions: use the defaults (needs -Iso and -Target; -Sd or -NoSd)

Everything is logged to setup.log next to this folder. Downloads are pinned by SHA-256 in
setup\toolchain.json and checked before use.
#>
param(
    [string]$Iso,
    [ValidateSet("", "switch", "pc", "both", "android", "all")][string]$Target = "",
    [ValidateSet("arm64-v8a", "x86_64", "both")][string]$Abi = "arm64-v8a",
    [ValidateSet("", "yes", "no")][string]$Install = "",
    [switch]$AcceptAndroidSdkLicense,
    [string]$Sd,
    [switch]$NoSd,
    [ValidateSet("", "yes", "no")][string]$CopyIso = "",
    [ValidateSet("", "yes", "no")][string]$DesktopShortcut = "",
    [string]$ToolsDir,
    [string]$Icon,
    [switch]$Yes
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"  # Invoke-WebRequest's progress bar slows downloads a lot
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root = (Resolve-Path "$PSScriptRoot\..").Path
$LogFile = Join-Path $Root "setup.log"
$IsoSha1 = "ce0cecf57034bef8bab7d6f588ce92c85a849613"
$SdSub = "switch\sonicriders"

# ---------------------------------------------------------------- logging, errors
"" | Out-File -FilePath $LogFile -Encoding utf8
# A log line must never stop the setup: another program (an antivirus scan, a log viewer) can hold
# setup.log open for a moment, and a failed write used to end the setup with no message.
function Write-LogFile([string]$text) {
    for ($i = 0; $i -lt 20; $i++) {
        try { [IO.File]::AppendAllText($LogFile, $text + "`r`n", [Text.Encoding]::UTF8); return } catch { Start-Sleep -Milliseconds 100 }
    }
}
function Log([string]$msg, [string]$color = "") {
    $line = "[{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $msg
    Write-LogFile $line
    if ($color) { Write-Host $msg -ForegroundColor $color } else { Write-Host $msg }
}
function Step([string]$msg) { Log ""; Log "== $msg" "Cyan" }
function Fail([string]$msg, [string]$hint = "") {
    Log ""
    Log "ERROR: $msg" "Red"
    if ($hint) { Log $hint "Yellow" }
    Log "The full log is in $LogFile (attach it if you report a problem; it contains no game data)." "Yellow"
    exit 1
}
function Ask([string]$question, [string]$default) {
    if ($Yes) { return $default }
    $a = Read-Host "$question [$default]"
    if ([string]::IsNullOrWhiteSpace($a)) { return $default }
    return $a.Trim()
}

# Run a program, show and log its output, stop on failure
function Run([string]$what, [string]$exe, [string[]]$arguments, [hashtable]$envVars = @{}) {
    Log "> $what"
    Write-LogFile ("  " + $exe + " " + ($arguments -join " "))
    $saved = @{}
    foreach ($k in $envVars.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $envVars[$k]) }
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        Push-Location $Root
        & $exe @arguments 2>&1 | ForEach-Object {
            $l = "$_"
            Write-LogFile ("  " + $l)
            Write-Host "  $l"
        }
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
        $ErrorActionPreference = $old
        foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
    }
    if ($code -ne 0) { Fail "$what failed (exit code $code)." }
}

Log "Sonic Riders native port - setup" "Green"
Log "Folder: $Root"
Log ("Windows {0}, PowerShell {1}" -f [Environment]::OSVersion.Version, $PSVersionTable.PSVersion)
if (-not [Environment]::Is64BitOperatingSystem) { Fail "64-bit Windows is required." }
if (-not (Test-Path "$Root\configure.py") -or -not (Test-Path "$Root\tools\recomp\gcrecomp.py")) {
    Fail "This does not look like the complete release folder (configure.py / tools are missing)." "Extract the whole zip first, then run SonicRidersSetup.bat from inside it."
}
# the build tools get paths in Windows' old 8-bit encoding: letters outside English (a user name
# with accented or Turkish letters in the folder) turned into "?" and every tool failed with "CreateProcess failed" (GBAtemp report)
if ($Root -match "[^ -~]") {
    Fail "The folder path has letters the build tools can't handle: $Root" "Extract the zip to a folder whose path uses only English letters and digits, e.g. C:\SonicRiders, and run the installer from there."
}
# the downloaded Python must not pick up a Python installed on this PC (its settings or packages)
foreach ($v in "PYTHONHOME", "PYTHONPATH", "PYTHONSTARTUP") { [Environment]::SetEnvironmentVariable($v, $null) }
$env:PYTHONNOUSERSITE = "1"
$curlExe = Join-Path $env:SystemRoot "System32\curl.exe"

# ---------------------------------------------------------------- choices
Step "1/7  Your disc image"
if (-not $Iso) {
    if ($Yes) { Fail "-Iso is required with -Yes." }
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = "Select your Sonic Riders (USA) disc image (GXEE8P, revision 0)"
    $dlg.Filter = "GameCube disc image (*.iso;*.gcm)|*.iso;*.gcm|All files (*.*)|*.*"
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Fail "No disc image selected." }
    $Iso = $dlg.FileName
}
if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) { Fail "Disc image not found: $Iso" }
$Iso = (Resolve-Path -LiteralPath $Iso).Path
$isoSize = (Get-Item -LiteralPath $Iso).Length
Log ("Image: {0} ({1:N0} bytes)" -f $Iso, $isoSize)
$fs = [IO.File]::OpenRead($Iso)
try { $hdr = New-Object byte[] 8; [void]$fs.Read($hdr, 0, 8) } finally { $fs.Close() }
$discId = [Text.Encoding]::ASCII.GetString($hdr, 0, 6)
if ($discId -ne "GXEE8P") {
    Fail "This image's game ID is '$discId'; Sonic Riders (USA) is GXEE8P." "Only the USA release is supported (PAL GXEP8P and Japanese GXEJ8P are not)."
}
if ($hdr[7] -ne 0) { Fail ("This is revision {0} of the disc; revision 0 is supported." -f $hdr[7]) }
Log "Checking the image's SHA-1 (about 10-30 seconds)..."
$sha1 = (Get-FileHash -LiteralPath $Iso -Algorithm SHA1).Hash.ToLower()
Log "SHA-1: $sha1"
if ($sha1 -ne $IsoSha1) {
    Fail "The image does not match the known-good dump (expected SHA-1 $IsoSha1)." "A clean 1:1 dump (Redump: 'Sonic Riders (USA) (En,Ja,Fr,De,Es,It)') is needed. Trimmed, compressed (RVZ/CISO/GCZ) or modified images do not work; convert RVZ back to ISO with Dolphin first."
}
Log "Disc image OK: Sonic Riders (USA), GXEE8P revision 0." "Green"

Step "2/7  What to build"
if (-not $Target) {
    Log "  1) Nintendo Switch (homebrew .nro, copied to your SD card)"
    Log "  2) Windows PC (play natively on this PC, no emulator)"
    Log "  3) Both"
    Log "  4) Android phone (APK, installed over USB; 64-bit phone with Android 11 or newer)"
    Log "  5) All three"
    $c = Ask "Choose 1, 2, 3, 4 or 5" "1"
    switch ($c) { "1" { $Target = "switch" } "2" { $Target = "pc" } "3" { $Target = "both" } "4" { $Target = "android" } "5" { $Target = "all" } default { Fail "Unknown choice '$c'." } }
}
$doSwitch = @("switch", "both", "all") -contains $Target
$doPc = @("pc", "both", "all") -contains $Target
$doAndroid = @("android", "all") -contains $Target
Log "Building: $Target"

$sdDir = $null
if ($doSwitch -and -not $NoSd) {
    if (-not $Sd) {
        if ($Yes) {
            Log "No -Sd given: the .nro stays in build\switch\ (copy it yourself)."
        } else {
            $a = Ask "Copy the build to your Switch's SD card now? (y/n)" "y"
            if ($a -match "^[yY]") {
                Add-Type -AssemblyName System.Windows.Forms
                $fb = New-Object System.Windows.Forms.FolderBrowserDialog
                $fb.Description = "Select your Switch SD card (its top folder, e.g. E:\)"
                $fb.ShowNewFolderButton = $false
                if ($fb.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $Sd = $fb.SelectedPath }
                else { Log "No SD card selected: the .nro stays in build\switch\." }
            }
        }
    }
    if ($Sd) {
        if (-not (Test-Path -LiteralPath $Sd -PathType Container)) { Fail "SD card path not found: $Sd" }
        $sdRoot = (Resolve-Path -LiteralPath $Sd).Path
        if ($sdRoot -match "\\switch\\sonicriders\\?$") { $sdRoot = Split-Path (Split-Path $sdRoot) }
        if (-not (Test-Path -LiteralPath (Join-Path $sdRoot "switch") -PathType Container) -and -not (Test-Path -LiteralPath (Join-Path $sdRoot "atmosphere"))) {
            $a = Ask "$sdRoot has no 'switch' folder. Is this really your Switch SD card? (y/n)" "n"
            if ($a -notmatch "^[yY]") { Fail "Stopped: pick the SD card's top folder (the one that contains 'switch' and 'atmosphere')." }
        }
        $sdDir = Join-Path $sdRoot $SdSub
        Log "SD card target: $sdDir"
        if (-not $CopyIso) {
            $existing = Get-ChildItem -LiteralPath $sdDir -Filter *.iso -ErrorAction SilentlyContinue | Where-Object { $_.Length -eq $isoSize }
            if ($existing) { $CopyIso = "no"; Log "The disc image is already on the SD card ($($existing[0].Name))." }
            else { $CopyIso = (Ask "Also copy your disc image (1.4 GB) to the SD card? It must be there once. (y/n)" "y") -replace "^[yY].*", "yes" -replace "^[nN].*", "no" }
        }
    }
}

# ---------------------------------------------------------------- toolchain
Step "3/7  Toolchain (downloaded once, checked by SHA-256)"
$tc = Get-Content -Raw (Join-Path $PSScriptRoot "toolchain.json") | ConvertFrom-Json
if (-not $ToolsDir) {
    $ToolsDir = Join-Path $env:LOCALAPPDATA "SonicRidersSetup\tools"
    # devkitPro paths must not contain spaces, and no tool path may have letters outside English
    if ($ToolsDir -match "[^!-~]") { $ToolsDir = Join-Path $env:SystemDrive "SonicRidersTools" }
}
if ($ToolsDir -match "[^!-~]") { Fail "The tools folder must not contain spaces or non-English letters: $ToolsDir" "Pass -ToolsDir C:\SonicRidersTools (or another such folder)." }
$dlDir = Join-Path $ToolsDir "downloads"
New-Item -ItemType Directory -Force -Path $dlDir | Out-Null
Log "Tools folder: $ToolsDir"
$drive = Get-PSDrive -Name ($Root.Substring(0, 1)) -ErrorAction SilentlyContinue
if ($drive -and $drive.Free -lt 4GB) { Log ("Warning: only {0:N1} GB free on {1}: - the build needs about 4 GB." -f ($drive.Free / 1GB), $drive.Name) "Yellow" }

function Download([string]$url, [string]$sha256, [string]$userAgent = "") {
    $name = [IO.Path]::GetFileName(([Uri]$url).AbsolutePath)
    $file = Join-Path $dlDir $name
    if (Test-Path $file) {
        if ((Get-FileHash $file -Algorithm SHA256).Hash.ToLower() -eq $sha256) { return $file }
        Remove-Item $file -Force
    }
    Log "Downloading $url"
    $tmp = "$file.part"
    # Windows' own curl.exe: some package servers (devkitPro's) refuse .NET's web client
    if (Test-Path $curlExe) {
        # a dropped connection (curl 56, "Connection was reset") is not one of the errors --retry repeats,
        # and a 1 GB download (the Android NDK) can meet one: try again, resuming the partial file (-C -)
        if (Test-Path $tmp) { Remove-Item $tmp -Force }
        $cargs = @("-fsSL", "--retry", "3", "-C", "-", "-o", $tmp)
        if ($userAgent) { $cargs += @("-A", $userAgent) }
        for ($try = 1; $try -le 5; $try++) {
            $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            $out = (& $curlExe @cargs $url 2>&1 | ForEach-Object { "$_" }) -join " "
            $ErrorActionPreference = $old
            if ($LASTEXITCODE -eq 0) { break }
            if ($LASTEXITCODE -eq 33 -and (Test-Path $tmp)) { Remove-Item $tmp -Force }  # server cannot resume
            if ($try -lt 5) {
                Log "  interrupted (curl exit code $LASTEXITCODE $out); trying again ($($try + 1) of 5)" "Yellow"
                Start-Sleep -Seconds (5 * $try)
            }
        }
        if ($LASTEXITCODE -ne 0) {
            if (Test-Path $tmp) { Remove-Item $tmp -Force }
            Fail "Download failed: $url (curl exit code $LASTEXITCODE $out)" "Check your internet connection (or a firewall/proxy), then run the setup again; finished downloads are kept."
        }
    } else {
        try {
            if ($userAgent) { Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing -UserAgent $userAgent }
            else { Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing }
        } catch {
            Fail "Download failed: $url ($($_.Exception.Message))" "Check your internet connection (or a firewall/proxy), then run the setup again; finished downloads are kept."
        }
    }
    $got = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower()
    if ($got -ne $sha256) {
        Remove-Item $tmp -Force
        Fail "Checksum mismatch for $name (got $got, expected $sha256)." "The download was corrupted or the file on the server changed. Try again; if it persists, report it."
    }
    Move-Item $tmp $file -Force
    Log "  verified (SHA-256 $sha256)"
    return $file
}

# Archives are unpacked without Windows' tar.exe: on many Windows versions it cannot read .tar.xz or
# .tar.zst and looks for xz.exe/zstd.exe on PATH instead. The Python zip is unpacked with .NET, the
# rest with tools\setup_extract.py on that Python (zstandard wheel for the .zst packages).
$python = $null; $zstdDir = $null
function Extract([string]$archive, [string]$dest) {
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    if (-not $python) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        try { [IO.Compression.ZipFile]::ExtractToDirectory($archive, $dest) }
        catch { Fail "Could not extract $archive ($($_.Exception.Message))" }
        return
    }
    $xargs = @("-I", (Join-Path $Root "tools\setup_extract.py"), $archive, $dest)
    if ($zstdDir) { $xargs += @("--zstd-dir", $zstdDir) }
    $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $out = (& $python @xargs 2>&1 | ForEach-Object { "$_" }) -join "`n"
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    if ($out) { Write-LogFile $out }
    if ($code -ne 0) { Fail "Could not extract $archive (exit code $code): $out" }
}

# a tool folder is complete when its marker holds the hash it was made from
function Ensure-Tool($spec) {
    $dir = Join-Path $ToolsDir $spec.dir
    $mark = Join-Path $dir ".setup-ok"
    if ((Test-Path $mark) -and ((Get-Content $mark -Raw).Trim() -eq $spec.sha256)) { return $dir }
    $file = Download $spec.url $spec.sha256
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    if ($spec.kind -eq "file") {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Copy-Item $file (Join-Path $dir $spec.name)
    } else {
        Log "Extracting $([IO.Path]::GetFileName($file))"
        $stage = "$dir.extract"
        if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
        Extract $file $stage
        $src = $stage
        if ($spec.strip) { $src = Join-Path $stage $spec.strip }
        New-Item -ItemType Directory -Force -Path (Split-Path $dir) | Out-Null
        Move-Item $src $dir
        if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    }
    Set-Content -Path $mark -Value $spec.sha256
    return $dir
}

$pyDir = Ensure-Tool $tc.python
# the embeddable Python ignores the script's folder for imports while python*._pth exists
Get-ChildItem $pyDir -Filter "python*._pth" | Remove-Item -Force
$python = Join-Path $pyDir "python.exe"
$zstdDir = Ensure-Tool $tc.zstandard
$ninja = Join-Path (Ensure-Tool $tc.ninja) "ninja.exe"
$dtk = Join-Path (Ensure-Tool $tc.dtk) "dtk.exe"
# dtk.exe needs the Visual C++ runtime (VCRUNTIME140.dll), which a clean Windows does not have
# (found by tools/sandbox_test.py). The embeddable Python ships the same DLL: put a copy next to dtk.
$vcrt = Join-Path (Split-Path $dtk) "vcruntime140.dll"
if (-not (Test-Path $vcrt)) { Copy-Item (Join-Path $pyDir "vcruntime140.dll") $vcrt }
$imgui = Ensure-Tool $tc.imgui
Log "Python, ninja, decomp-toolkit and Dear ImGui ready."

$dkp = $null
if ($doSwitch) {
    $dkpSpec = $tc.devkitpro
    $dkp = Join-Path $ToolsDir $dkpSpec.dir
    $want = ($dkpSpec.packages | ForEach-Object { $_.sha256 }) -join ","
    $mark = Join-Path $dkp ".setup-ok"
    if (-not ((Test-Path $mark) -and ((Get-Content $mark -Raw).Trim() -eq $want))) {
        $stage = "$dkp.extract"
        if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
        foreach ($p in $dkpSpec.packages) {
            $f = Download $p.url $p.sha256 $dkpSpec.user_agent
            Log "Extracting $([IO.Path]::GetFileName($f))"
            Extract $f $stage
        }
        if (Test-Path $dkp) { Remove-Item $dkp -Recurse -Force }
        Move-Item (Join-Path $stage "opt\devkitpro") $dkp
        Remove-Item $stage -Recurse -Force
        Set-Content -Path $mark -Value $want
    }
    if (-not (Test-Path (Join-Path $dkp "devkitA64\bin\aarch64-none-elf-gcc.exe"))) { Fail "The Switch toolchain is incomplete in $dkp" "Delete that folder and run the setup again." }
    Log "Switch toolchain ready (devkitA64, libnx, deko3d, uam, switch-tools)."
}
$llvm = $null; $sdl = $null
if ($doPc) {
    $llvm = Ensure-Tool $tc.llvm_mingw
    $sdl = Ensure-Tool $tc.sdl2
    Log "PC toolchain ready (llvm-mingw, SDL2)."
}
$asdk = $null; $jdk = $null; $sdlSrc = $null; $adb = $null
if ($doAndroid) {
    if (-not $AcceptAndroidSdkLicense) {
        Log "The Android build uses Google's Android SDK and NDK. Using them means accepting the Android SDK License:"
        Log "  https://developer.android.com/studio/terms"
        if ($Yes) { Fail "-Yes does not accept the Android SDK License for you." "Read it and add -AcceptAndroidSdkLicense, or run without -Yes." }
        $a = Ask "Do you accept the Android SDK License? (y/n)" "n"
        if ($a -notmatch "^[yY]") { Fail "The Android build needs the Android SDK License accepted." }
    }
    Log "Android SDK License accepted."
    $an = $tc.android
    foreach ($k in "ndk", "build_tools", "platform", "platform_tools") { [void](Ensure-Tool $an.$k) }
    $asdk = Join-Path $ToolsDir "android-sdk"
    $jdk = Ensure-Tool $an.jdk
    $sdlSrc = Ensure-Tool $an.sdl2_src
    $adb = Join-Path $asdk "platform-tools\adb.exe"
    if (-not (Test-Path (Join-Path $asdk "ndk\27.3.13750724\ndk-build.cmd"))) { Fail "The Android NDK is incomplete in $asdk" "Delete that folder and run the setup again." }
    Log "Android toolchain ready (NDK r27d, SDK build-tools 35, platform 35, JDK 17, SDL2 source)."
}

# ---------------------------------------------------------------- recompile
$needGame = $doSwitch -or $doPc -or $doAndroid
if ($needGame) {
Step "4/7  Extracting the game code from your disc image"
$baserom = Join-Path $Root "baserom"
New-Item -ItemType Directory -Force -Path $baserom | Out-Null
Run "extract main.dol and _Main.rel" $python @("-I", "tools\extract_iso.py", $Iso, "baserom", "--no-verify-iso")
if ($doPc) {
    # the PC program's icon: the game's own save icon, from your disc (configure.py builds it in)
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "build") | Out-Null
    $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $out = (& $python -I (Join-Path $Root "tools\make_icon.py") (Join-Path $baserom "_Main.rel") (Join-Path $Root "build\icon.ico") 2>&1 | ForEach-Object { "$_" }) -join " "
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    Write-LogFile "  $out"
    if ($code -ne 0) { Log "Could not make the program icon ($out); the game works without it." "Yellow" }
}

Step "5/7  Analysing the game code"
Run "decomp-toolkit analysis" $dtk @("dol", "split", "config/GXEE8P/config.yml", "build/GXEE8P")
}
# the build recompiles the code to C (configure.py the first time, ninja when its inputs change)

Step "6/7  Recompiling and building (a few minutes)"
$builtNro = $null; $builtExe = $null  # (set when built)
if ($doSwitch) {
    Run "configure (Switch)" $python @("configure.py", "--target", "switch", "--devkitpro", $dkp, "--imgui", $imgui) @{ DEVKITPRO = $dkp }
    Run "build the Switch homebrew (this takes a while)" $ninja @() @{ DEVKITPRO = $dkp }
    $builtNro = Join-Path $Root "build\switch\sonicriders.nro"
    if (-not (Test-Path $builtNro)) { Fail "The build finished but build\switch\sonicriders.nro is missing." }
    Log ("Switch build: {0} ({1:N0} bytes)" -f $builtNro, (Get-Item $builtNro).Length) "Green"
}
if ($doPc) {
    $pcArgs = @("configure.py", "--target", "host", "--toolchain", "mingw", "--llvm-mingw", $llvm, "--sdl", $sdl, "--imgui", $imgui)
    if ($Icon) { $pcArgs += @("--icon", $Icon) }
    Run "configure (PC)" $python $pcArgs
    Run "build the PC test build" $ninja @()
    $builtExe = Join-Path $Root "build\host-mingw\sonicriders.exe"
    if (-not (Test-Path $builtExe)) { Fail "The build finished but build\host-mingw\sonicriders.exe is missing." }
    # the PC build reads the first *.iso in baserom\: link (same drive) or copy the image there
    $isoDest = Join-Path $baserom ([IO.Path]::GetFileName($Iso))
    if (-not (Get-ChildItem $baserom -Filter *.iso | Where-Object { $_.Length -eq $isoSize })) {
        try { New-Item -ItemType HardLink -Path $isoDest -Target $Iso | Out-Null; Log "Linked the disc image into baserom\." }
        catch { Log "Copying the disc image into baserom\ ..."; Copy-Item -LiteralPath $Iso $isoDest }
    }
    Log "PC build: $builtExe" "Green"
}
$builtApks = @()
if ($doAndroid) {
    $keystore = Join-Path $ToolsDir "android\sonicriders-local.keystore"
    foreach ($a in $(if ($Abi -eq "both") { @("arm64-v8a", "x86_64") } else { @($Abi) })) {
        $andArgs = @("configure.py", "--target", "android", "--android-sdk", $asdk, "--jdk", $jdk, "--sdl-src", $sdlSrc, "--abi", $a, "--keystore", $keystore, "--imgui", $imgui)
        if ($Icon) { $andArgs += @("--icon", $Icon) }  # else the save icon from baserom\_Main.rel
        Run "configure (Android, $a)" $python $andArgs
        Run "build the Android APK ($a; this takes a while)" $ninja @()
        $apk = Join-Path $Root "build\android\sonicriders-$a.apk"
        if (-not (Test-Path $apk)) { Fail "The build finished but build\android\sonicriders-$a.apk is missing." }
        $builtApks += $apk
        Log ("Android build: {0} ({1:N0} bytes)" -f $apk, (Get-Item $apk).Length) "Green"
    }
    Log "Your signing key is ${keystore}: keep it. An update signed with another key cannot replace the installed app." "Yellow"
}

# ---------------------------------------------------------------- install
Step "7/7  Installing"
if ($sdDir) {
    New-Item -ItemType Directory -Force -Path $sdDir | Out-Null
    $nroDest = Join-Path $sdDir "sonicriders.nro"
    Copy-Item $builtNro $nroDest -Force
    if ((Get-FileHash $nroDest -Algorithm SHA256).Hash -ne (Get-FileHash $builtNro -Algorithm SHA256).Hash) { Fail "The copy on the SD card does not match the build (card full or failing?)." }
    Log "Copied sonicriders.nro to $sdDir (verified)."
    if ($CopyIso -eq "yes") {
        $isoSd = Join-Path $sdDir ([IO.Path]::GetFileName($Iso))
        $sdFree = (Get-PSDrive -Name ($sdDir.Substring(0, 1)) -ErrorAction SilentlyContinue).Free
        if ($sdFree -and $sdFree -lt $isoSize) { Fail "Not enough space on the SD card for the disc image." }
        Log "Copying the disc image to the SD card (1.4 GB, a few minutes)..."
        Copy-Item -LiteralPath $Iso $isoSd -Force
        if ((Get-Item -LiteralPath $isoSd).Length -ne $isoSize) { Fail "The disc image copy on the SD card is incomplete." }
        Log "Copied the disc image (checking it, about a minute)..."
        if ((Get-FileHash -LiteralPath $isoSd -Algorithm SHA1).Hash.ToLower() -ne $IsoSha1) { Fail "The disc image on the SD card does not match (card failing?)." }
        Log "Disc image on the SD card verified." "Green"
    }
    Log ""
    Log "Done. Eject the SD card safely, put it in the Switch, hold R while starting any game to open the" "Green"
    Log "Homebrew Menu with full memory, and start 'Sonic Riders'." "Green"
} elseif ($builtNro) {
    Log "Switch: copy build\switch\sonicriders.nro and your disc image to sdmc:/switch/sonicriders/ on the SD card." "Green"
}
if ($builtExe) {
    # shortcuts start the game in this folder (its disc image link, saves\ and settings live here)
    $old = Join-Path $Root "Play on PC.bat"
    if (Test-Path $old) { Remove-Item $old -Force }
    function New-Shortcut([string]$path) {
        $ws = New-Object -ComObject WScript.Shell
        $lnk = $ws.CreateShortcut($path)
        $lnk.TargetPath = $builtExe
        $lnk.WorkingDirectory = $Root
        $lnk.Description = "Sonic Riders (native PC port)"
        $lnk.Save()
    }
    $here = Join-Path $Root "Sonic Riders.lnk"
    try { New-Shortcut $here; Log "Created the shortcut 'Sonic Riders' in this folder." } catch { Log "Could not create a shortcut ($($_.Exception.Message)); start build\host-mingw\sonicriders.exe from this folder instead." "Yellow" }
    if (-not $DesktopShortcut) { $DesktopShortcut = (Ask "Put a 'Sonic Riders' shortcut on your desktop? (y/n)" "y") -replace "^[yY].*", "yes" -replace "^[nN].*", "no" }
    if ($DesktopShortcut -eq "yes") {
        try { New-Shortcut (Join-Path ([Environment]::GetFolderPath("Desktop")) "Sonic Riders.lnk"); Log "Created the desktop shortcut." } catch { Log "Could not create the desktop shortcut." "Yellow" }
    }
    Log ""
    Log "PC: start the game with the 'Sonic Riders' shortcut. Keep this folder: the game, your disc image" "Green"
    Log "link and your saves (saves\) live here." "Green"
    Log "Keyboard: WASD = move, J = A, K = B, L = X, I = Y, Enter = START, U/O = L/R, P = Z." "Green"
    Log "A game controller works too. Hold F1 (or the controller's Back/View button) for the settings menu;" "Green"
    Log "F11 or Alt+Enter switches fullscreen." "Green"
}
if ($builtApks.Count -gt 0) {
    $phoneApk = $builtApks | Where-Object { $_ -match "arm64-v8a" } | Select-Object -First 1
    if (-not $phoneApk) { $phoneApk = $builtApks[0] }
    $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    # "adb devices" starts the adb server, which keeps running (and holds the tools folder) after the
    # setup: skip it when the answer is already no, and stop it at the end
    $devs = @(); $unauth = @()
    if ($Install -ne "no") {
        $adbLines = @(& $adb devices 2>$null | Select-Object -Skip 1)
        $devs = @($adbLines | Where-Object { $_ -match "\tdevice$" })
        $unauth = @($adbLines | Where-Object { $_ -match "\tunauthorized$" })
    }
    $ErrorActionPreference = $old
    if (-not $Install) {
        if ($devs.Count -eq 1) { $Install = (Ask "Install it on the connected Android device now? (y/n)" "y") -replace "^[yY].*", "yes" -replace "^[nN].*", "no" }
        else {
            # say why nothing is installed: a silent skip looks like the setup jumped to the end (#14)
            $Install = "no"
            if ($devs.Count -gt 1) {
                Log "Not installing: $($devs.Count) Android devices are connected; leave only the phone connected and run the setup again." "Yellow"
            } elseif ($unauth.Count -gt 0) {
                Log "Not installing: the phone is connected but has not allowed this PC yet. Unlock it, tap Allow on the" "Yellow"
                Log "'Allow USB debugging?' prompt, then run the setup again (the build is reused, so it goes straight to installing)." "Yellow"
            } else {
                Log "Not installing: no Android phone found. Turn on USB debugging (Settings > Developer options), connect the" "Yellow"
                Log "phone by USB, allow this PC on the phone's screen, then run the setup again (the build is reused)." "Yellow"
            }
        }
    }
    if ($Install -eq "yes") {
        if ($devs.Count -ne 1) { Fail "Install: $($devs.Count) Android devices are connected and authorized; exactly one is needed." "Turn on USB debugging, connect the phone, allow this PC on its screen, then run the setup again." }
        Run "install on the Android device" $adb @("install", "-r", $phoneApk)
        # the game reads the disc image from its own folder on the phone (docs/ANDROID_PLAN.md section 5)
        $phoneDir = "/sdcard/Android/data/net.srnative.sonicriders/files/baserom"
        $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        $have = (& $adb shell "sha1sum $phoneDir/*.iso 2>/dev/null") -join " "
        $ErrorActionPreference = $old
        if ($have -match $IsoSha1) {
            Log "The disc image is already on the phone."
        } else {
            # the app must create its own folder: one made from the adb shell first is not readable by the app
            $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            & $adb shell "am start -W -n net.srnative.sonicriders/net.srnative.sonicriders.SonicRidersActivity" 2>&1 | Out-Null
            Start-Sleep -Seconds 4
            & $adb shell "am force-stop net.srnative.sonicriders" 2>&1 | Out-Null
            $ErrorActionPreference = $old
            Run "make the game's folder on the phone" $adb @("shell", "mkdir -p $phoneDir")
            Log "Copying the disc image to the phone (1.4 GB, about a minute over USB)..."
            Run "copy the disc image to the phone" $adb @("push", $Iso, "$phoneDir/SonicRiders.iso")
            $have = (& $adb shell "sha1sum $phoneDir/SonicRiders.iso") -join " "
            if ($have -notmatch $IsoSha1) { Fail "The disc image on the phone does not match (storage full?)." }
            Log "Disc image on the phone verified." "Green"
        }
        Log "Installed. Start 'Sonic Riders' from the phone's app list. Tap the top-left button for the settings menu." "Green"
    } else {
        Log "Android: install $phoneApk with 'adb install -r' (USB debugging on)." "Green"
    }
    if ($Install -ne "no") {
        $old = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        & $adb kill-server 2>&1 | Out-Null
        $ErrorActionPreference = $old
    }
}
if (-not $sdDir -and -not $builtNro -and -not $builtExe -and $builtApks.Count -eq 0) { Log "Done." "Green" }
Log "Log: $LogFile"
exit 0
