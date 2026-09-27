# Connect to the VirtualBox serial TCP server as a two-way terminal.
#
# Run from CMD:
#   pwsh -ExecutionPolicy Bypass -File .\tools\scripts\vbox-serial.ps1
#
# Requires the VM to be configured with:
#   VBox modifyvm NotYVOS --uart1 0x3F8 4
#   VBox modifyvm NotYVOS --uartmode1 tcpserver 2323

param(
    [string]$TcpHost    = "127.0.0.1",
    [int]   $TcpPort    = 2323,
    [int]   $WaitSecs   = 60
)

$ErrorActionPreference = "Stop"

Write-Host "Waiting for ${TcpHost}:${TcpPort} (up to $WaitSecs seconds) ..."

$client   = New-Object System.Net.Sockets.TcpClient
$deadline = (Get-Date).AddSeconds($WaitSecs)
$connected = $false

while ((Get-Date) -lt $deadline) {
    try {
        $client.Connect($TcpHost, $TcpPort)
        $connected = $true
        break
    } catch {
        Start-Sleep -Milliseconds 500
    }
}

if (-not $connected) {
    Write-Host ""
    Write-Host "Could not connect after $WaitSecs seconds." -ForegroundColor Red
    Write-Host "Possible causes:" -ForegroundColor Yellow
    Write-Host "  1. The VM is not running."
    Write-Host "  2. The VM was configured without a TCP serial port."
    Write-Host "     Fix: VBox modifyvm NotYVOS --uart1 0x3F8 4"
    Write-Host "          VBox modifyvm NotYVOS --uartmode1 tcpserver 2323"
    Write-Host "  3. Another program is using port 2323."
    Write-Host "     Check: netstat -ano | findstr :2323"
    exit 1
}

Write-Host "Connected. Type to send. Ctrl+C to exit." -ForegroundColor Green
Write-Host ""

$stream = $client.GetStream()

# Keep the console in raw mode so single keystrokes are delivered.
$oldTreatControlC = [Console]::TreatControlCAsInput
[Console]::TreatControlCAsInput = $false

$buf = New-Object byte[] 1024

try {
    while ($client.Connected) {
        # Incoming from guest
        while ($stream.DataAvailable) {
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            $text = [System.Text.Encoding]::ASCII.GetString($buf, 0, $n)
            Write-Host -NoNewline $text
        }

        # Outgoing to guest
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            $ch = $key.KeyChar
            if ($key.Key -eq [ConsoleKey]::Enter) { $ch = "`n" }
            $bytes = [System.Text.Encoding]::ASCII.GetBytes([string]$ch)
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
        }

        Start-Sleep -Milliseconds 10
    }
} finally {
    [Console]::TreatControlCAsInput = $oldTreatControlC
    $client.Close()
}
