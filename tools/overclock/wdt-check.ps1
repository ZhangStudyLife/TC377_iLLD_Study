param([int]$Fault)
$ErrorActionPreference = 'Stop'
$nm = & 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-nm.exe' -S Debug/TC377_iLLD_Study.elf
$address = (($nm | Where-Object {$_ -match '\sfaultCommand$'}) -split '\s+')[0]
& Debug/overclock-mcd.exe f000047c=3000 "$address=$Fault" > "Debug/wdt-fixed-$Fault-inject.log"
if ($LASTEXITCODE) { throw 'Fault injection failed' }
Start-Sleep -Seconds 20
& Debug/overclock-mcd.exe f881fe08 f0036050 f0036a20 > "Debug/wdt-fixed-$Fault-reset.log"
if ($LASTEXITCODE) { & Debug/overclock-mcd.exe f881fe08 f0036050 f0036a20 > "Debug/wdt-fixed-$Fault-reset.log" }
if (!(Select-String -Path "Debug/wdt-fixed-$Fault-reset.log" -Pattern '^state=1$' -Quiet)) { throw 'Watchdog did not resume autonomously' }
Start-Sleep -Seconds 3
& Debug/overclock-read.ps1 -Label "wdt-fixed-$Fault" > "Debug/wdt-fixed-$Fault-summary.log"
if ($LASTEXITCODE) { & Debug/overclock-read.ps1 -Label "wdt-fixed-$Fault" > "Debug/wdt-fixed-$Fault-summary.log" }
$snapshot = Get-Content "Debug/overclock-wdt-fixed-$Fault-result.json" -Raw | ConvertFrom-Json -AsHashtable
$reset = Get-Content "Debug/wdt-fixed-$Fault-reset.log" -Raw
$reason = [Convert]::ToUInt32([regex]::Match($reset,'address=f0036050 u32=([0-9a-f]+)').Groups[1].Value,16)
$alarm = [Convert]::ToUInt32([regex]::Match($reset,'address=f0036a20 u32=([0-9a-f]+)').Groups[1].Value,16)
$mask = @(0x400,0x800,0x1000,0x10000)[$Fault-1]
if (!($reason -band 8) -or !($alarm -band $mask) -or $snapshot['faultCommand[0]'] -ne 0 -or $snapshot['mbistPassed[0]'] -ne 3 -or $snapshot.coreState -ne '1') { throw 'Watchdog reset/recovery failed' }
Get-Content "Debug/wdt-fixed-$Fault-reset.log" | Select-String '^state=|^address='
Get-Content "Debug/overclock-wdt-fixed-$Fault-result.json" | Select-String 'uptime|coreState|bootReset|faultCommand|mbistPassed|Errors'
