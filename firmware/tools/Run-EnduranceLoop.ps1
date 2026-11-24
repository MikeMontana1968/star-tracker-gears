# Keep endurance runs going back to back (tools\usb_bridge.py must be running).
# Each run is Run-Endurance.ps1 (its own .log/.csv). When one ends FATAL, this
# restarts the board into CONFIG_AP, clears the fault and starts the next run.
# It gives up when a run dies in under -MinRunMin minutes -MaxQuick times in a row
# (a fault that repeats at once is real, not a blip), or on the STOP file.
#   .\Run-EnduranceLoop.ps1 [-WaitForExisting] [-MinRunMin 30] [-MaxQuick 3]
param([switch]$WaitForExisting, [int]$MinRunMin = 30, [int]$MaxQuick = 3,
      [string]$Out = (Join-Path $PSScriptRoot "..\..\docs\calibration\endurance"),
      [string]$Base = "http://127.0.0.1:8780")
$ErrorActionPreference = "Continue"
Add-Type -Namespace Win32 -Name Power2 -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);'
[Win32.Power2]::SetThreadExecutionState([uint32]"0x80000001") | Out-Null
$loopLog = Join-Path $Out "endurance_loop.log"
$stop = Join-Path $Out "STOP"
function Write-Loop([string]$m) {
  $line = "{0:yyyy-MM-dd HH:mm:ss}  [loop]  {1}" -f (Get-Date), $m
  $line | Out-File $loopLog -Append -Encoding utf8; Write-Output $line
}
# A run still going = the newest run log has no closing line yet. (Process
# command lines can't be trusted: a launcher may hide the script name.)
function Get-Runners {
  $last = Get-ChildItem $Out -Filter "endurance_*.log" | Where-Object Name -ne "endurance_loop.log" | Sort-Object LastWriteTime | Select-Object -Last 1
  if (-not $last) { return $false }
  $t = Get-Content $last.FullName -Raw -Encoding utf8
  return -not ($t -match '\]  ran [\d.]+ h' -or $t -match 'ended cleanly after')
}
function Reset-Board {
  try { Invoke-WebRequest "$Base/bridge/console" -Method POST -Body '{"line":"A"}' -UseBasicParsing -TimeoutSec 20 | Out-Null } catch { Write-Loop "restart request failed: $_" }
  $until = (Get-Date).AddMinutes(10)
  while ((Get-Date) -lt $until) {
    Start-Sleep -Seconds 5
    try { $s = Invoke-RestMethod "$Base/api/status" -TimeoutSec 5 } catch { continue }
    if ($s.state -eq "CONFIG_AP") {
      try { Invoke-RestMethod "$Base/api/command" -Method POST -ContentType application/json -Body '{"cmd":"clear_fault"}' -TimeoutSec 10 | Out-Null } catch {}
      $s = Invoke-RestMethod "$Base/api/status" -TimeoutSec 5
      Write-Loop ("board reset: state {0} fault {1} pos {2:F3} enc {3:F3} vbat {4:F2}" -f $s.state, $s.fault_code, $s.pos_deg, $s.encoder_deg, $s.battery_v)
      return ($s.fault_code -eq 0)
    }
  }
  Write-Loop "board did not come back to CONFIG_AP in 10 min"; return $false
}

if ($WaitForExisting) {
  Write-Loop "waiting for the running endurance test to end"
  while (Get-Runners) { if (Test-Path $stop) { break }; Start-Sleep -Seconds 30 }
  if (Test-Path $stop) { Write-Loop "STOP file: ending"; exit 0 }
  Write-Loop "the running test ended; taking over"
  if (-not (Reset-Board)) { Write-Loop "giving up: board not ready"; exit 1 }
}
$quick = 0; $n = 0
while ($true) {
  if (Test-Path $stop) { Write-Loop "STOP file: ending"; break }
  $n++; $t0 = Get-Date
  Write-Loop "run $n starting"
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "Run-Endurance.ps1") -Out $Out -Base $Base | Out-Null
  $code = $LASTEXITCODE; $min = ((Get-Date) - $t0).TotalMinutes
  $last = Get-ChildItem $Out -Filter "endurance_*.log" | Where-Object Name -ne "endurance_loop.log" | Sort-Object LastWriteTime | Select-Object -Last 1
  $why = if ($last) { (Get-Content $last.FullName -Encoding utf8 | Select-String "FATAL" | Select-Object -Last 1).Line } else { "" }
  Write-Loop ("run {0} ended (exit {1}) after {2:F1} h: {3}" -f $n, $code, ($min / 60), $why)
  if ($code -eq 0 -or (Test-Path $stop)) { Write-Loop "ended cleanly"; break }
  if ($min -lt $MinRunMin) { $quick++ } else { $quick = 0 }
  if ($quick -ge $MaxQuick) { Write-Loop "giving up: $quick runs in a row died in under $MinRunMin min"; exit 1 }
  if (-not (Reset-Board)) { Start-Sleep -Seconds 60; if (-not (Reset-Board)) { Write-Loop "giving up: board not ready"; exit 1 } }
}
