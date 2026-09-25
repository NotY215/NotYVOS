# Fetch Limine for NOTYVOS.
#   - source tarball  -> third_party/limine/limine-<tag>/        (for limine.h)
#   - binary release  -> third_party/limine/limine-binary/       (boot files)
# No `make` required.
#
# The Limine v12.x binary release does NOT ship the `limine` host tool.
# The host tool is only needed for BIOS El Torito patching. NOTYVOS boots
# via UEFI, so we build a UEFI-only ISO and skip BIOS support at Phase 0.

$ErrorActionPreference = "Stop"

$Version   = "v12.9.0"
$Tag       = $Version.TrimStart("v")
$Canonical = "limine-$Tag"

$Root      = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Root      = Join-Path $Root "third_party\limine"
New-Item -ItemType Directory -Force -Path $Root | Out-Null

$SrcDir    = Join-Path $Root $Canonical
$BinDir    = Join-Path $Root "limine-binary"

# ---------------------------------------------------------------------------
# 1. Source (limine.h)
# ---------------------------------------------------------------------------
if (-not (Test-Path (Join-Path $SrcDir "limine.h"))) {

    Get-ChildItem -Path $Root -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^[Ll]imine-' -and $_.Name -ne "limine-binary" } |
        ForEach-Object {
            Write-Host "Removing stale source dir: $($_.FullName)"
            Remove-Item -Recurse -Force $_.FullName
        }

    $SrcTar = Join-Path $Root "limine-$Tag.tar.gz"
    $SrcUrl = "https://github.com/Limine-Bootloader/Limine/releases/download/$Version/limine-$Tag.tar.gz"

    Write-Host "Downloading Limine source $Version ..."
    Invoke-WebRequest -Uri $SrcUrl -OutFile $SrcTar -UseBasicParsing

    Write-Host "Extracting source (tar -xzf) ..."
    tar -xzf $SrcTar -C $Root
    if ($LASTEXITCODE -ne 0) { throw "tar extraction failed (exit $LASTEXITCODE)" }

    $extracted = Get-ChildItem -Path $Root -Directory |
                 Where-Object { $_.Name -match "^[Ll]imine-$([regex]::Escape($Tag))$" } |
                 Select-Object -First 1

    if (-not $extracted) {
        $tree = (Get-ChildItem -Path $Root -Recurse -Depth 2 -ErrorAction SilentlyContinue |
                 Select-Object -First 40 | ForEach-Object { $_.FullName }) -join "`n"
        throw "Limine source directory not found. Tree under ${Root}:`n$tree"
    }

    if ($extracted.Name -ne $Canonical) {
        if (Test-Path $SrcDir) { Remove-Item -Recurse -Force $SrcDir }
        Write-Host "Renaming $($extracted.Name) -> $Canonical"
        Rename-Item -Path $extracted.FullName -NewName $Canonical
    }

    $ProtocolUrl = "https://raw.githubusercontent.com/Limine-Bootloader/limine-protocol/trunk/include/limine.h"
    Write-Host "Downloading Limine protocol header ..."
    Invoke-WebRequest -Uri $ProtocolUrl -OutFile (Join-Path $SrcDir "limine.h") -UseBasicParsing
}

if (-not (Test-Path (Join-Path $SrcDir "limine.h"))) {
    throw "Source extraction failed: $SrcDir\limine.h not found."
}
Write-Host "Limine source ready: $SrcDir"

# ---------------------------------------------------------------------------
# 2. Binary release
# ---------------------------------------------------------------------------
$haveBin = Test-Path (Join-Path $BinDir "BOOTX64.EFI")
if (-not $haveBin) {
    $BinTar = Join-Path $Root "limine-binary-$Tag.tar.gz"
    $BinUrl = "https://github.com/Limine-Bootloader/Limine/releases/download/$Version/limine-binary.tar.gz"

    Write-Host "Downloading Limine binary release $Version ..."
    Invoke-WebRequest -Uri $BinUrl -OutFile $BinTar -UseBasicParsing

    Write-Host "Extracting binary release ..."
    $Tmp = Join-Path $Root ".bin-extract"
    if (Test-Path $Tmp) { Remove-Item -Recurse -Force $Tmp }
    New-Item -ItemType Directory -Force -Path $Tmp | Out-Null
    tar -xzf $BinTar -C $Tmp
    if ($LASTEXITCODE -ne 0) { throw "binary tar extraction failed (exit $LASTEXITCODE)" }

    $found = Get-ChildItem -Path $Tmp -Recurse -Filter "BOOTX64.EFI" -File |
             Select-Object -First 1
    if (-not $found) {
        $listing = (Get-ChildItem -Path $Tmp -Recurse |
                    Select-Object -First 30 | ForEach-Object { $_.FullName }) -join "`n"
        throw "BOOTX64.EFI not found in the binary release. Contents:`n$listing"
    }

    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    Copy-Item -Path (Join-Path $found.Directory.FullName "*") `
              -Destination $BinDir -Recurse -Force
    Remove-Item -Recurse -Force $Tmp
} else {
    Write-Host "Limine binary already present: $BinDir"
}

# ---------------------------------------------------------------------------
# 3. Sanity check — boot files only (host tool is optional)
# ---------------------------------------------------------------------------
$required = @("BOOTX64.EFI", "limine-uefi-cd.bin")
$missing = @()
foreach ($f in $required) {
    if (-not (Test-Path (Join-Path $BinDir $f))) { $missing += $f }
}
if ($missing.Count -gt 0) {
    Write-Warning "Missing from binary release: $($missing -join ', ')"
    exit 2
}

$hostTool = $null
foreach ($candidate in @("limine.exe", "limine")) {
    $p = Join-Path $BinDir $candidate
    if (Test-Path $p) { $hostTool = $p; break }
}

Write-Host ""
if ($hostTool) {
    Write-Host "Limine $Version ready (BIOS + UEFI)."
    Write-Host "  source:    $SrcDir"
    Write-Host "  binaries:  $BinDir"
    Write-Host "  host tool: $hostTool"
} else {
    Write-Host "Limine $Version ready (UEFI-only)."
    Write-Host "  source:    $SrcDir"
    Write-Host "  binaries:  $BinDir"
    Write-Host "  host tool: not present (expected; UEFI boot unaffected)"
    Write-Host ""
    Write-Host "NOTYVOS will build a UEFI-only ISO. This is intentional."
}
exit 0
