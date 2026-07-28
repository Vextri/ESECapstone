$port = New-Object System.IO.Ports.SerialPort COM13,115200,None,8,One
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Write-Host "Monitoring COM13 -- press Ctrl+C to stop"
while ($true) {
    if ($port.BytesToRead -gt 0) {
        Write-Host -NoNewline $port.ReadExisting()
    }
    Start-Sleep -Milliseconds 50
}
