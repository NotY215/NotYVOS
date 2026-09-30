# NOTYVOS serial console.
#
# Connects to the VM's COM1 (exposed by VirtualBox in tcpserver mode on
# the given host/port), reads guest bytes to stdout, and forwards host
# keystrokes back to the guest. Uses a simple polling loop so it works
# on Windows PowerShell 5.1 and PowerShell 7.x without extra modules.

param(
    [string]$HostName = "127.0.0.1",
    [int]   $Port     = 2323
)

$ErrorActionPreference = "Stop"

$client = New-Object System.Net.Sockets.TcpClient
try {
    $client.Connect($HostName, $Port)
} catch {
    Write-Host "ERROR: could not connect to ${HostName}:${Port}: $_"
    exit 1
}

$stream = $client.GetStream()
Write-Host "Connected. Type to send. Ctrl+C to exit."

$readBuf = New-Object byte[] 4096
$encoding = [System.Text.Encoding]::ASCII

try {
    while ($true) {
        # Drain anything the guest has sent.
        $drained = $false
        while ($stream.DataAvailable) {
            $n = $stream.Read($readBuf, 0, $readBuf.Length)
            if ($n -le 0) { throw "socket closed" }
            $text = $encoding.GetString($readBuf, 0, $n)
            [Console]::Out.Write($text)
            $drained = $true
        }
        if ($drained) { [Console]::Out.Flush() }

        # Forward host keystrokes.
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            $ch  = $key.KeyChar
            $bytes = $encoding.GetBytes([string]$ch)
            if ($bytes.Length -gt 0) {
                $stream.Write($bytes, 0, $bytes.Length)
                $stream.Flush()
            }
            if ($key.Key -eq [ConsoleKey]::Enter) {
                $nl = $encoding.GetBytes("`n")
                $stream.Write($nl, 0, $nl.Length)
                $stream.Flush()
            }
        } else {
            Start-Sleep -Milliseconds 10
        }
    }
} catch {
    Write-Host ""
    Write-Host "Disconnected: $_"
} finally {
    try { $stream.Close() } catch {}
    try { $client.Close() } catch {}
}
