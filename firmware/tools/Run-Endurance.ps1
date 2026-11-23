# Endurance test through the USB bridge (tools\usb_bridge.py must be running).
#   cycle: re-home -> track LUNAR 3 h -> re-home -> track SOLAR 6 h -> repeat
# "re-home" = restart into CONFIG_AP (the boot re-seeds the step counter from the
# encoder map; the counter-minus-encoder gap is logged first: the error that night
# accumulated) and then Home (a move to park).
# Stops on the first FATAL: a fault, a night that did not complete, a failed home,
# or the bridge/board unreachable for 10 min. Or create the STOP file to end it
# after the current step.
#   .\Run-Endurance.ps1 [-LunarMin 180] [-SolarMin 360] [-Out <dir>]
param([int]$LunarMin = 180, [int]$SolarMin = 360,
      [string]$Out = (Join-Path $PSScriptRoot "..\..\docs\calibration\endurance"),
      [string]$Base = "http://127.0.0.1:8780")
$ErrorActionPreference = "Stop"
# keep the PC from idle-sleeping while this script runs (released when it exits;
# no power settings are changed). ES_CONTINUOUS | ES_SYSTEM_REQUIRED
Add-Type -Namespace Win32 -Name Power -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);'
[Win32.Power]::SetThreadExecutionState([uint32]"0x80000001") | Out-Null
New-Item -ItemType Directory -Force $Out | Out-Null
$stamp = Get-Date -Format "yyyy-MM-dd_HHmm"
$csv = Join-Path $Out "endurance_$stamp.csv"
$log = Join-Path $Out "endurance_$stamp.log"
$stop = Join-Path $Out "STOP"
Remove-Item $stop -ErrorAction SilentlyContinue
"time,cycle,phase,state,pos_deg,enc_deg,enc_err_deg,expected_deg,vbat,fault,guard,driver_cs" | Out-File $csv -Encoding utf8
$script:cycle = 0; $script:phase = "start"; $script:t0 = Get-Date

function Write-Log([string]$m) {
  $line = "{0:yyyy-MM-dd HH:mm:ss}  [{1}/{2}]  {3}" -f (Get-Date), $script:cycle, $script:phase, $m
  $line | Out-File $log -Append -Encoding utf8
  Write-Output $line
}
function Invoke-Api([string]$method, [string]$path, $body = $null) {
  $args2 = @{ Uri = "$Base$path"; Method = $method; UseBasicParsing = $true; TimeoutSec = 20 }
  if ($null -ne $body) { $args2.Body = ($body | ConvertTo-Json -Compress); $args2.ContentType = "application/json" }
  try { return ((Invoke-WebRequest @args2).Content | ConvertFrom-Json) }
  catch {
    $msg = $_.ErrorDetails.Message
    if ($msg) { try { return ($msg | ConvertFrom-Json) } catch {} }
    return $null
  }
}
$script:lastOk = Get-Date
function Get-Status {
  $s = Invoke-Api GET "/api/status"
  if ($s) {
    $script:lastOk = Get-Date
    $exp = if ($s.night) { $s.night.expected_deg } else { "" }
    "{0:yyyy-MM-dd HH:mm:ss},{1},{2},{3},{4:F4},{5:F4},{6:F4},{7},{8:F2},{9},{10},{11}" -f (Get-Date), $script:cycle, $script:phase,
      $s.state, $s.pos_deg, $s.encoder_deg, $s.encoder_err_deg, $exp, $s.battery_v, $s.fault_text, $s.guard, $s.driver.cs_actual |
      Out-File $csv -Append -Encoding utf8
  } elseif (((Get-Date) - $script:lastOk).TotalMinutes -gt 10) { Stop-Fatal "bridge/board unreachable for 10 min" }
  return $s
}
function Stop-Fatal([string]$why) {
  Write-Log "FATAL: $why"
  $s = Invoke-Api GET "/api/status"
  if ($s) { Write-Log ("final: state {0} fault '{1}' pos {2:F3} enc {3:F3} guard '{4}' vbat {5:F2}" -f $s.state, $s.fault_text, $s.pos_deg, $s.encoder_deg, $s.guard, $s.battery_v) }
  $lg = Invoke-Api GET "/api/log"
  if ($lg) { $lg.lines | Select-Object -Last 15 | ForEach-Object { Write-Log ("  fsm: {0} {1}" -f $_.utc, $_.msg) } }
  Write-Log ("ran {0:F2} h" -f ((Get-Date) - $script:t0).TotalHours)
  exit 1
}
function Wait-For([scriptblock]$done, [int]$timeoutMin, [string]$what, [int]$pollS = 30) {
  $until = (Get-Date).AddMinutes($timeoutMin)
  while ((Get-Date) -lt $until) {
    $s = Get-Status
    if ($s) {
      if ($s.fault_code -ne 0) { Stop-Fatal "fault '$($s.fault_text)' while $what" }
      if (& $done $s) { return $s }
    }
    Start-Sleep -Seconds $pollS
  }
  Stop-Fatal "timed out after $timeoutMin min: $what"
}
function Invoke-Rehome {
  $script:phase = "rehome"
  $s = Get-Status
  if ($s) { Write-Log ("before re-home: pos {0:F4} enc {1:F4} -> counter minus encoder {2:F4} deg" -f $s.pos_deg, $s.encoder_deg, ($s.pos_deg - $s.encoder_deg)) }
  Invoke-WebRequest "$Base/bridge/console" -Method POST -Body '{"line":"A"}' -UseBasicParsing -TimeoutSec 20 | Out-Null
  Start-Sleep -Seconds 20
  $s = Wait-For { param($x) $x.state -eq "CONFIG_AP" } 30 "restarting into CONFIG_AP" 10
  Write-Log ("re-seeded from the encoder: pos {0:F4} enc {1:F4}" -f $s.pos_deg, $s.encoder_deg)
  $r = Invoke-Api POST "/api/command" @{ cmd = "home" }
  if (-not $r -or -not $r.ok) { Stop-Fatal "home refused: $($r.msg)" }
  Start-Sleep -Seconds 5
  $s = Wait-For { param($x) $x.state -eq "CONFIG_AP" -and [math]::Abs($x.pos_deg) -lt 0.01 } 25 "homing to park" 10
  Start-Sleep -Seconds 10
  $s = Get-Status
  if ($s.move_failed) { Stop-Fatal "home move failed" }
  Write-Log ("homed: pos {0:F4} enc {1:F4} (enc-err {2:F4}) vbat {3:F2}" -f $s.pos_deg, $s.encoder_deg, $s.encoder_err_deg, $s.battery_v)
}
function Invoke-Track([string]$rate, [int]$minutes) {
  $script:phase = "$rate"
  $r = Invoke-Api POST "/api/command" @{ cmd = "test_track"; minutes = $minutes; rate = $rate }
  if (-not $r -or -not $r.ok) { Stop-Fatal "test_track $rate refused: $($r.msg)" }
  Write-Log "test track: $rate for $minutes min"
  # started: wait for the night to appear, then for it to finish (back in CONFIG_AP, no night)
  Wait-For { param($x) $null -ne $x.night } 5 "the $rate night to start" 10 | Out-Null
  $s = Wait-For { param($x) $null -eq $x.night -and $x.state -eq "CONFIG_AP" } ($minutes + 60) "the $rate night" 30
  $n = Invoke-Api GET "/api/night/last"
  Write-Log ("night: outcome {0} reason '{1}' tracked {2:F2} h, max div {3:F3} deg, min vbat {4:F2}, warnings {5}" -f $n.outcome, $n.reason,
    (($n.track_stop - $n.track_start) / 3600.0), $n.max_div_deg, $n.min_battery_v, $n.warnings)
  if ($n.outcome -ne "completed") { Stop-Fatal "the $rate night ended '$($n.outcome)': $($n.reason)" }
  $lg = Invoke-Api GET "/api/log"
  if ($lg) { $lg.lines | Where-Object { $_.msg -match "divergence|GUARD|slip|warn" } | Select-Object -Last 5 | ForEach-Object { Write-Log "  fsm: $($_.msg)" } }
}

Write-Log "endurance start: lunar $LunarMin min, solar $SolarMin min per cycle; csv $csv"
while ($true) {
  $script:cycle++
  Invoke-Rehome
  if (Test-Path $stop) { Write-Log "STOP file: ending"; break }
  Invoke-Track "lunar" $LunarMin
  Invoke-Rehome
  if (Test-Path $stop) { Write-Log "STOP file: ending"; break }
  Invoke-Track "solar" $SolarMin
  Write-Log ("cycle {0} done; {1:F2} h so far" -f $script:cycle, ((Get-Date) - $script:t0).TotalHours)
  if (Test-Path $stop) { Write-Log "STOP file: ending"; break }
}
Invoke-Rehome
Write-Log ("ended cleanly after {0:F2} h" -f ((Get-Date) - $script:t0).TotalHours)
