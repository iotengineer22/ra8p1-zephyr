$port = New-Object System.IO.Ports.SerialPort "COM8", 115200, "None", 8, "One"
$port.ReadTimeout = 1000
try {
    $port.Open()
    $port.DiscardInBuffer()
    # Read output continuously for 2 seconds
    $deadline = (Get-Date).AddSeconds(2)
    $sb = New-Object System.Text.StringBuilder
    while ((Get-Date) -lt $deadline) {
        $line = $port.ReadExisting()
        if ($line.Length -gt 0) {
            [void]$sb.Append($line)
        }
        Start-Sleep -Milliseconds 50
    }
    Write-Output $sb.ToString()
} catch {
    Write-Output ("Error: " + $_.Exception.Message)
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
