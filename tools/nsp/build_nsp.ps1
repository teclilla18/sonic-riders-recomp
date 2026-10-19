# Build the Sonic Riders home-screen forwarder NSP and copy it to the Switch SD card.
#
# The NSP holds only a launcher (devkitPro's nx-hbloader, patched to start
# sdmc:/switch/sonicriders/sonicriders.nro and to exit when the game returns), an icon and the
# title info. It contains no game data. It is signed with the user's own keys and must never be
# committed or shared.
#
# Inputs (prepared beforehand, all gitignored):
#   <scratch>\hbl\hbl.nso, hbl.npdm       the built launcher (title id 010053520D9A0000)
#   <scratch>\hbp\                        hacBrewPack sources (compiled here with zig cc)
#   build\nsp\control\                    control.nacp + icon_AmericanEnglish.dat
#   <path to prod.keys>
# Output: build\nsp\out\SonicRiders.nsp, copied to <sd>\switch\sonicriders\SonicRiders.nsp
param(
    [string]$Scratch = "<scratch>",
    [string]$Sd = "<sd>\switch\sonicriders"
)
$ErrorActionPreference = "Stop"
$Root = "<repo>"
$Nsp = "$Root\build\nsp"
$TitleId = "010053520d9a0000"

# 1. compile hacBrewPack (and its bundled mbedtls) with zig's MinGW C compiler
Push-Location "$Scratch\hbp"
if (-not (Test-Path hacbrewpack.exe)) {
    $mbed = Get-ChildItem mbedtls\library\*.c | ForEach-Object { "mbedtls/library/" + $_.Name }
    python -m ziglang cc -target x86_64-windows-gnu -O2 -std=gnu11 -w -D_BSD_SOURCE -D_POSIX_SOURCE `
        -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE -D__USE_MINGW_ANSI_STDIO=1 -D_FILE_OFFSET_BITS=64 `
        -I mbedtls/include -o hacbrewpack.exe sha.c aes.c extkeys.c pki.c utils.c main.c filepath.c `
        ConvertUTF.c nca.c romfs.c pfs0.c ivfc.c nacp.c npdm.c cnmt.c rsa.c @mbed -lbcrypt -lws2_32
    if (-not (Test-Path hacbrewpack.exe)) { throw "hacBrewPack did not build" }
}
Pop-Location

# 2. lay out exefs (launcher) next to the prepared control folder
New-Item -ItemType Directory -Force "$Nsp\exefs" | Out-Null
Copy-Item "$Scratch\hbl\hbl.nso" "$Nsp\exefs\main" -Force
Copy-Item "$Scratch\hbl\hbl.npdm" "$Nsp\exefs\main.npdm" -Force
Remove-Item -Recurse -Force "$Nsp\out", "$Nsp\nca", "$Nsp\temp", "$Nsp\backup" -ErrorAction SilentlyContinue

# 3. pack
Push-Location $Nsp
& "$Scratch\hbp\hacbrewpack.exe" -k "$Root\<path to prod.keys>" `
    --titleid $TitleId --exefsdir exefs --controldir control --noromfs --nologo `
    --nspdir out --ncadir nca --tempdir temp --backupdir backup
Pop-Location
$built = Get-ChildItem "$Nsp\out\*.nsp" | Select-Object -First 1
if (-not $built) { throw "no NSP was produced" }
Move-Item $built.FullName "$Nsp\out\SonicRiders.nsp" -Force
"built $Nsp\out\SonicRiders.nsp ($((Get-Item "$Nsp\out\SonicRiders.nsp").Length) bytes)"

# 4. copy to the SD card (only inside switch\sonicriders), verify, flush
if (Test-Path $Sd) {
    Copy-Item "$Nsp\out\SonicRiders.nsp" "$Sd\SonicRiders.nsp" -Force
    $a = (Get-FileHash "$Nsp\out\SonicRiders.nsp").Hash; $b = (Get-FileHash "$Sd\SonicRiders.nsp").Hash
    if ($a -ne $b) { throw "SD copy does not match" }
    Write-VolumeCache -DriveLetter $Sd.Substring(0, 1)
    "copied to $Sd\SonicRiders.nsp and verified; the SD card is safe to remove"
} else {
    "SD card not found at $Sd; copy build\nsp\out\SonicRiders.nsp to switch\sonicriders\ yourself"
}
