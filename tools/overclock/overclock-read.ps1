param([string]$Label = 'snapshot')
$ErrorActionPreference = 'Stop'
$root = 'D:/ACS_WorkSpace/TC377_iLLD_Study'
$symbols = & 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-nm.exe' -S "$root/Debug/TC377_iLLD_Study.elf"
$map = @()
foreach ($name in @('cpu0Hz','cpu1Hz','cpu2Hz','sriHz','spbHz','stmHz','gtmHz','gtmCmuHz','gtmTom1Hz','pwmTickHz','ips200SpiHz','uptimeSeconds','stmInterrupts','stmLate','stmMaxLateTicks','stressLoops','stressErrors','pwmSamples','pwmErrors','pwmStage','pwmPeriodTicks','pwmDutyTicks','pwmMeasuredHz','pwmMeasuredDuty','screenFrames','qspiErrorFlags','ipcPackets','ipcErrors','ipcInterrupts','atomicChecks','uartBytes','uartErrors','smuInterrupts','smuErrors','watchdogServices','faultCommand','bootReset','bootAlarms','alarmSeen','platformReady','mbistPassed','mbistErrors','mbistStatus','mbistAddress','sshBoot','sshSeen','treeHz','dieTemperature','peakTemperature','ccuInterrupts','ccuErrors','dmaChecks','dmaErrors','crcChecks','crcErrors','canFrames','canErrors','gptChecks','gptErrors')) {
    $fields = (($symbols | Where-Object {$_ -match ('\s' + $name + '$')}) -split '\s+')
    if (-not $fields[0]) { continue }
    $count = if ($name -in @('stressLoops','stressErrors','ipcPackets','ipcErrors','ipcInterrupts','atomicChecks')) {3} elseif ($name -in @('pwmSamples','pwmErrors')) {9} elseif ($name -in @('bootAlarms','alarmSeen')) {12} elseif ($name -eq 'canFrames') {4} elseif ($name -in @('sshBoot','sshSeen')) {37} elseif ($name -eq 'treeHz') {13} elseif ($name -eq 'watchdogServices') {4} else {1}
    for ($i = 0; $i -lt $count; $i++) {
        $map += [pscustomobject]@{name="$name[$i]";address=([Convert]::ToUInt32($fields[0],16)+$i*4).ToString('x')}
    }
}
$addresses = @($map | ForEach-Object {$_.address})
$read = & "$root/Debug/overclock-mcd.exe" @addresses
$read | Set-Content "$root/Debug/overclock-$Label-read.log"
if ($LASTEXITCODE -ne 0) { $read | Select-Object -Last 8; exit 1 }
$data = [ordered]@{timestamp=(Get-Date -Format o)}
$data['coreState'] = ($read | Select-String '^state=(\d+)$').Matches.Groups[1].Value
$values = @($read | Select-String '^address=')
for ($i = 0; $i -lt $values.Count; $i++) {
    if ($values[$i].Line -match 'u32=([0-9a-f]+) float=([^ ]+)') {
        $integer = [Convert]::ToUInt32($Matches[1],16)
        $float = $Matches[2]
        $name = $map[$i].name
        $data[$name] = if ($name -match 'Hz|MeasuredDuty|Temperature') {$float} else {$integer}
    }
}
$data | ConvertTo-Json | Set-Content "$root/Debug/overclock-$Label-result.json"
$data | ConvertTo-Json
