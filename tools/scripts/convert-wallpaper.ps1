# Convert wallpaper/1.png to a raw RGB buffer packed into initramfs.
# Format: u32 width LE, u32 height LE, then width*height*4 bytes BGRA.
# This runs at build time on the host so the kernel needs no PNG decoder.

param(
    [string]$Src = "wallpaper\1.png",
    [string]$Dst = "wallpaper.raw"
)

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Src)) {
    Write-Host "No wallpaper at $Src, skipping."
    exit 0
}

$img = [System.Drawing.Image]::FromFile($Src)
$w = $img.Width
$h = $img.Height

$bmp = New-Object System.Drawing.Bitmap($img)
$rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
$data = $bmp.LockBits($rect,
    [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

$stride = $data.Stride
$bytes = New-Object byte[] ($stride * $h)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$bmp.UnlockBits($data)
$bmp.Dispose()
$img.Dispose()

$out = [System.IO.File]::Create($Dst)
$bw = New-Object System.IO.BinaryWriter($out)
$bw.Write([UInt32]$w)
$bw.Write([UInt32]$h)
for ($y = 0; $y -lt $h; $y++) {
    $offset = $y * $stride
    $bw.Write($bytes, $offset, $w * 4)
}
$bw.Close()
$out.Close()

Write-Host "Converted $Src -> $Dst ($w x $h)"
