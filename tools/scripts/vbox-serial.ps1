param(
    [string]$TcpHost  = "127.0.0.1",
    [int]   $TcpPort  = 2323,
    [int]   $WaitSecs = 60
)

$ErrorActionPreference = "Continue"

function Connect-Once {
    param([string]$H, [int]$P, [int]$W)
    Write-Host "Waiting for ${H}:${P} (up to $W s) ..."
    $deadline = (Get-Date).AddSeconds($W)
    while ((Get-Date) -lt $deadline) {
        try {
            $c = New-Object System.Net.Sockets.TcpClient
            $c.Connect($H, $P)
            return $c
        } catch {
            Start-Sleep -Milliseconds 500
        }
    }
    return $null
}

$client = Connect-Once -H $TcpHost -P $TcpPort -W $WaitSecs
if (-not $client) {
    Write-Host "Could not connect. Is the VM running?" -ForegroundColor Red
    exit 1
}
Write-Host "Connected. Type to send. Ctrl+C to exit." -ForegroundColor Green

try {
    while ($true) {
        $stream = $client.GetStream()

        # Incoming loop
        $buf = New-Object byte[] 1024
        $disconnected = $false
        while (-not $disconnected) {
            try {
                while ($stream.DataAvailable) {
                    $n = $stream.Read($buf, 0, $buf.Length)
                    if ($n -le 0) { $disconnected = $true; break }
                    $text = [System.Text.Encoding]::ASCII.GetString($buf, 0, $n)
                    Write-Host -NoNewline $text
                }
                if ([Console]::KeyAvailable) {
                    $key = [Console]::ReadKey($true)
                    $ch = $key.KeyChar
                    if ($key.Key -eq [ConsoleKey]::Enter) { $ch = "`n" }
                    $bytes = [System.Text.Encoding]::ASCII.GetBytes([string]$ch)
                    try {
                        $stream.Write($bytes, 0, $bytes.Length)
                        $stream.Flush()
                    } catch {
                        $disconnected = $true
                    }
                }
                Start-Sleep -Milliseconds 20
            } catch {
                $disconnected = $true
            }
        }

        Write-Host ""
        Write-Host "[serial disconnected, reconnecting ...]" -ForegroundColor Yellow
        try { $client.Close() } catch {}
        Start-Sleep -Seconds 1
        $client = Connect-Once -H $TcpHost -P $TcpPort -W 30
        if (-not $client) { Write-Host "Reconnect failed. Exiting." -ForegroundColor Red; exit 1 }
        Write-Host "[serial reconnected]" -ForegroundColor Green
    }
} finally {
    try { $client.Close() } catch {}
}
