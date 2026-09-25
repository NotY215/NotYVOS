# Fetch Limine for NOTYVOS.
#   - source tarball  -> third_party/limine/limine-<tag>/        (for limine.h)
#   - binary release  -> third_party/limine/limine-binary/       (boot files + host tool)
# No `make` required.
#
# GitHub archive tarballs extract to a folder named after the repository
# with its original capitalization ("Limine-<tag>" here, not "limine-<tag>").
# This script normalizes the folder name to "limine-<tag>" so that
# cmake/limine.cmake and kernel/CMakeLists.txt have a single deterministic path.

$ErrorActionPreference = "Stop"

$Version   = "v12.9.0"
$Tag       = $Version.TrimStart("v")           # -> "12.9.0"
$Canonical = "limine-$Tag"                     # we enforce this folder name

$Root      = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Root      = Join-Path $Root "third_party\limine"
New-Item -ItemType Directory -Force -Path $Root | Out-Null

$SrcDir    = Join-Path $Root $Canonical
$BinDir    = Join-Path $Root "limine-binary"

# ---------------------------------------------------------------------------
# 1. Source tarball (for limine.h)
# ---------------------------------------------------------------------------
if (-not (Test-Path (Join-Path $SrcDir "limine.h"))) {

    # Remove any stale extracted source (case variants, previous tags).
    Get-ChildItem -Path $Root -Directory -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -match '^[Ll]imine-' -and $_.Name -ne "limine-binary"
        } |
        ForEach-Object {
            Write-Host "Removing stale source dir: $($_.FullName)"
            Remove-Item -Recurse -Force $_.FullName
        }

    $SrcTar = Join-Path $Root "limine-$Version.tar.gz"
    $SrcUrl = "https://github.com/Limine-Bootloader/Limine/archive/refs/tags/$Version.tar.gz"

    Write-Host "Downloading Limine source $Version ..."
    Invoke-WebRequest -Uri $SrcUrl -OutFile $SrcTar -UseBasicParsing

    Write-Host "Extracting source (tar -xzf) ..."
    tar -xzf $SrcTar -C $Root
    if ($LASTEXITCODE -ne 0) { throw "tar extraction failed (exit $LASTEXITCODE)" }

    # Find whichever folder GitHub produced (Limine-<tag> or limine-<tag>).
    $extracted = Get-ChildItem -Path $Root -Directory |
                 Where-Object {
                     $_.Name -match "^[Ll]imine-$([regex]::Escape($Tag))$"
                 } |
                 Select-Object -First 1

    # Fallback: locate limine.h anywhere under $Root (excluding limine-binary).
    if (-not $extracted) {
        $hdr = Get-ChildItem -Path $Root -Recurse -Filter "limine.h" -File `
                             -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -notlike "*\limine-binary\*" } |
               Select-Object -First 1
        if (-not $hdr) {
            $tree = (Get-ChildItem -Path $Root -Recurse -Depth 2 -ErrorAction SilentlyContinue |
                     Select-Object -First 40 |
                     ForEach-Object { $_.FullName }) -join "`n"
            throw "limine.h not found after extraction. Tree under $Root:`n$tree"
        }
        $extracted = $hdr.Directory
    }

    # Normalize folder name to $Canonical.
    if ($extracted.Name -ne $Canonical) {
        if (Test-Path $SrcDir) { Remove-Item -Recurse -Force $SrcDir }
        Write-Host "Renaming $($extracted.Name) -> $Canonical"
        Rename-Item -Path $extracted.FullName -NewName $Canonical
    }
}

if (-not (Test-Path (Join-Path $SrcDir "limine.h"))) {
    throw "Source extraction failed: $SrcDir\limine.h not found."
}
Write-Host "Limine source ready: $SrcDir"

# ---------------------------------------------------------------------------
# 2. Prebuilt binary release (for BOOTX64.EFI etc.)
# ---------------------------------------------------------------------------
$haveBin = Test-Path (Join-Path $BinDir "BOOTX64.EFI")
if (-not $haveBin) {
    $BinZip = Join-Path $Root "limine-binary-$Version.zip"
    $BinUrl = "https://github.com/Limine-Bootloader/Limine/releases/download/$Version/limine-binary.zip"

    Write-Host "Downloading Limine binary release $Version ..."
    Invoke-WebRequest -Uri $BinUrl -OutFile $BinZip -UseBasicParsing

    Write-Host "Extracting binary release ..."
    $Tmp = Join-Path $Root ".bin-extract"
    if (Test-Path $Tmp) { Remove-Item -Recurse -Force $Tmp }
    New-Item -ItemType Directory -Force -Path $Tmp | Out-Null
    Expand-Archive -Path $BinZip -DestinationPath $Tmp -Force

    $found = Get-ChildItem -Path $Tmp -Recurse -Filter "BOOTX64.EFI" -File |
             Select-Object -First 1
    if (-not $found) {
        $listing = (Get-ChildItem -Path $Tmp -Recurse |
                    Select-Object -First 30 |
                    ForEach-Object { $_.FullName }) -join "`n"
        throw "BOOTX64.EFI not found in the binary zip. Contents:`n$listing"
    }

    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    Copy-Item -Path (Join-Path $found.Directory.FullName "*") `
              -Destination $BinDir -Recurse -Force
    Remove-Item -Recurse -Force $Tmp
} else {
    Write-Host "Limine binary already present: $BinDir"
}

# ---------------------------------------------------------------------------
# 3. Sanity check
# ---------------------------------------------------------------------------
$required = @(
    "BOOTX64.EFI",
    "limine-bios.sys",
    "limine-bios-cd.bin",
    "limine-uefi-cd.bin"
)
$missing = @()
foreach ($f in $required) {
    if (-not (Test-Path (Join-Path $BinDir $f))) { $missing += $f }
}
if ($missing.Count -gt 0) {
    Write-Warning "Missing from binary release: $($missing -join ', ')"
    Write-Warning "Check https://github.com/Limine-Bootloader/Limine/releases for $Version layout."
    exit 2
}

$hostTool = $null
foreach ($candidate in @("limine.exe", "limine")) {
    $p = Join-Path $BinDir $candidate
    if (Test-Path $p) { $hostTool = $p; break }
}
if (-not $hostTool) {
    Write-Warning "Host tool 'limine' / 'limine.exe' not found in $BinDir."
    exit 3
}

Write-Host ""
Write-Host "Limine $Version ready."
Write-Host "  source:    $SrcDir"
Write-Host "  binaries:  $BinDir"
Write-Host "  host tool: $hostTool"
