# Fetch the pinned Limine release into third_party/limine/limine-<version>/
# Requires: PowerShell 5+, Invoke-WebRequest, tar (Windows 10+ builtin).

$ErrorActionPreference = "Stop"

$Version  = "v8.6.0"
$Root     = Join-Path $PSScriptRoot "..\..\third_party\limine"
$SrcDir   = Join-Path $Root "limine-$Version"
$Archive  = Join-Path $Root "limine-$Version.tar.gz"
$Url      = "https://github.com/limine-bootloader/limine/archive/refs/tags/$Version.tar.gz"

if (Test-Path (Join-Path $SrcDir "Makefile")) {
    Write-Host "Limine $Version already present at $SrcDir"
    exit 0
}

New-Item -ItemType Directory -Force -Path $Root | Out-Null

Write-Host "Downloading Limine $Version ..."
Invoke-WebRequest -Uri $Url -OutFile $Archive

Write-Host "Extracting ..."
tar -xzf $Archive -C $Root

if (-not (Test-Path (Join-Path $SrcDir "Makefile"))) {
    throw "Extraction failed: $SrcDir does not contain a Makefile."
}

Write-Host "Building Limine host tool + boot files ..."
Push-Location $SrcDir
try {
    # Limine's Makefile builds the limine installer, limine-bios.sys,
    # BOOTX64.EFI, and limine-bios-cd.bin. Requires make + (on Windows)
    # an MSYS2/MinGW environment on PATH, or the prebuilt release.
    #
    # To avoid requiring a POSIX make on Windows, prefer the prebuilt
    # binary release instead of building from source:
    #   https://github.com/limine-bootloader/limine/releases
    #
    # We fetch the prebuilt binary release to avoid a hard MSYS2 dep:
    Write-Warning "Building from source requires make. If this fails, use the prebuilt release binary."
    make
} finally {
    Pop-Location
}

Write-Host "Done. Limine at: $SrcDir"