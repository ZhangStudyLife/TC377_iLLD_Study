param([string]$Label)
$ErrorActionPreference = 'Stop'
$port = New-Object System.IO.Ports.SerialPort COM10,115200,None,8,One
$port.ReadTimeout = 3000
$port.WriteTimeout = 3000
$sent = New-Object byte[] 256
$received = New-Object byte[] 256
$random = New-Object System.Random 500
$bytes = 0
$errors = 0
$watch = [Diagnostics.Stopwatch]::StartNew()
try {
    $port.Open()
    $port.DiscardInBuffer()
    for ($block = 0; $block -lt 4096; $block++) {
        $random.NextBytes($sent)
        $port.Write($sent,0,256)
        $count = 0
        while ($count -lt 256) { $count += $port.Read($received,$count,256-$count) }
        for ($i = 0; $i -lt 256; $i++) { if ($sent[$i] -ne $received[$i]) { $errors++ } }
        $bytes += 256
    }
} finally {
    $port.Close()
    [ordered]@{timestamp=(Get-Date -Format o);port='COM10';baud=115200;bytes=$bytes;errors=$errors;seconds=$watch.Elapsed.TotalSeconds} | ConvertTo-Json | Set-Content "Debug/uart-$Label-result.json"
}
