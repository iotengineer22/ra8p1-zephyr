$port = New-Object System.IO.Ports.SerialPort "COM8", 115200, "None", 8, "One"
$port.ReadTimeout = 500
$port.Open()
$port.DiscardInBuffer()
Start-Sleep -Milliseconds 100


# Run JLink reset
$jlinkProcess = Start-Process -FilePath "C:\Program Files\SEGGER\JLink_V942\JLink.exe" -ArgumentList "-device R7KA8P1KF -if SWD -speed 4000 -autoconnect 1 -CommanderScript C:\Users\ioten\tron_zephyr\yoloface_npu\reset.jlink" -PassThru -NoNewWindow
$jlinkProcess.WaitForExit()

# Read serial output over the next 6 seconds
$deadline = (Get-Date).AddSeconds(6)

$sb = New-Object System.Text.StringBuilder
while ((Get-Date) -lt $deadline) {
    try {
        $line = $port.ReadExisting()
        if ($line.Length -gt 0) {
            [void]$sb.Append($line)
        }
    } catch { }
    Start-Sleep -Milliseconds 100
}

if ($port.IsOpen) {
    $port.Close()
}

Write-Output $sb.ToString()
