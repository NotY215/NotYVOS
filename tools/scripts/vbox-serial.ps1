param(
    [string]$HostName = "127.0.0.1",
    [int]   $Port     = 2323
)

$ErrorActionPreference = "Continue"

$encoding = [System.Text.Encoding]::ASCII
$readBuf  = New-Object byte[] 4096

function Connect-Once {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.Connect($HostName, $Port)
        return $client
    } catch {
        try { $client.Close() } catch {}
        return $null
    }
}

function Run-Client {
    param([System.Net.Sockets.TcpClient]$client)

    $stream = $client.GetStream()
    $client.NoDelay = $true
    Write-Host "Connected. Type to send. Ctrl+C to exit."

    try {
        while ($true) {
            $drained = $false
            while ($stream.DataAvailable) {
                $n = $stream.Read($readBuf, 0, $readBuf.Length)
                if ($n -le 0) { throw "socket closed by remote" }
                $text = $encoding.GetString($readBuf, 0, $n)
                [Console]::Out.Write($text)
                $drained = $true
            }
            if ($drained) { [Console]::Out.Flush() }

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
                Start-Sleep -Milliseconds 8
            }
        }
    } catch {
        Write-Host ""
        Write-Host "Disconnected: $_"
    } finally {
        try { $stream.Close() } catch {}
        try { $client.Close() } catch {}
    }
}

# Outer reconnect loop. VirtualBox closes the TCP server on VM reset and
# reopens it when the VM boots again. We auto-reconnect so the user does
# not lose their console window after a Start -> Restart from the desktop.
while ($true) {
    $client = $null
    while (-not $client) {
        $client = Connect-Once
        if (-not $client) { Start-Sleep -Milliseconds 400 }
    }
    Run-Client -client $client
    Write-Host "Reconnecting in 1 s..."
    Start-Sleep -Seconds 1
}
