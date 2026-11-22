<#
  Run-Calibration.ps1 -- the gear-train calibration suite, over the USB serial
  console's `H` API (no Wi-Fi needed). The SAME moves every time, so runs on
  different mechanical builds compare like for like (docs/calibration/).

    .\Run-Calibration.ps1                     # the full suite, ~75 min
    .\Run-Calibration.ps1 -Suite park         # just the lash-at-park block, ~12 min
    .\Run-Calibration.ps1 -Port COM10 -Out ..\..\docs\calibration\run.log

  Suites (blocks run in this order for "all"):
    park    repeat 5 deg x4 at overshoot 0 / default / 0.7 / 1.0  -> lash at park
    arc     default-overshoot wiggles at 0 44 45.5 47 48.5 90 135 178 -> lash vs angle
    speed   60 deg sweeps at 3200 (x2), 2000, 1200 pulses/s        -> steps lost vs speed
    full    one 180 deg sweep at the firmware default              -> whole-travel following + rewind

  Needs: the tracker firmware (pio run -e esp32-oled), the board awake (bench
  stay-awake, or press the config button), and the COM port free (close the
  Schedule Studio bridge). Moves the gear train through its whole travel:
  nothing may be in the way of the camera arm.
#>
param([ValidateSet("all", "park", "arc", "speed", "full")][string]$Suite = "all",
      [string]$Port = "", [string]$Out = "")
$ErrorActionPreference = "Stop"

if (-not $Port) {
  $dev = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'CH340.*\((COM\d+)\)' } | Select-Object -First 1
  if (-not $dev) { throw "no CH340 serial port found -- is the tracker plugged in?" }
  $Port = [regex]::Match($dev.Name, 'COM\d+').Value
}
if (-not $Out) { $Out = Join-Path $PSScriptRoot ("calibration_{0:yyyy-MM-dd_HHmm}.log" -f (Get-Date)) }

$blocks = [ordered]@{
  park  = @('{"cmd":"diag","kind":"repeat","deg":5,"cycles":4,"overshoot_deg":0}',
            '{"cmd":"diag","kind":"repeat","deg":5,"cycles":4}',
            '{"cmd":"diag","kind":"repeat","deg":5,"cycles":4,"overshoot_deg":0.7}',
            '{"cmd":"diag","kind":"repeat","deg":5,"cycles":4,"overshoot_deg":1.0}')
  arc   = @('goto 0', '{"cmd":"diag","kind":"wiggle"}',
            'goto 44', '{"cmd":"diag","kind":"wiggle"}', 'goto 45.5', '{"cmd":"diag","kind":"wiggle"}',
            'goto 47', '{"cmd":"diag","kind":"wiggle"}', 'goto 48.5', '{"cmd":"diag","kind":"wiggle"}',
            'goto 90', '{"cmd":"diag","kind":"wiggle"}', 'goto 135', '{"cmd":"diag","kind":"wiggle"}',
            'goto 178', '{"cmd":"diag","kind":"wiggle"}', 'goto 0')
  speed = @('{"cmd":"diag","kind":"sweep","deg":60}', '{"cmd":"diag","kind":"sweep","deg":60}',
            '{"cmd":"diag","kind":"sweep","deg":60,"slew_hz":2000}', '{"cmd":"diag","kind":"sweep","deg":60,"slew_hz":1200}')
  full  = @('{"cmd":"diag","kind":"sweep","deg":180}')
}
$plan = if ($Suite -eq "all") { $blocks.Values | ForEach-Object { $_ } } else { $blocks[$Suite] }

$p = New-Object System.IO.Ports.SerialPort $Port, 115200
$p.DtrEnable = $false; $p.RtsEnable = $false; $p.ReadBufferSize = 262144; $p.NewLine = "`n"
$p.Open()
function W($s) { Add-Content $Out $s; Write-Output $s }
function Drain { $s = $p.ReadExisting(); if ($s) { Add-Content $Out $s -NoNewline }; $s }
function Send($line, $waitMs = 1500) { $p.WriteLine($line); Start-Sleep -Milliseconds $waitMs; Drain }
function Status {
  $o = Send "H GET /api/status" 1200
  $m = [regex]::Match($o, 'HTTP 200 (\{.*\})')
  if ($m.Success) { try { return ($m.Groups[1].Value | ConvertFrom-Json) } catch {} }
  $null
}
function WaitDiag($maxMin) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.Elapsed.TotalMinutes -lt $maxMin) {
    Start-Sleep -Seconds 5
    $s = Status
    if ($s -and $s.state -ne 'CONFIG_AP') { W "!! left CONFIG_AP: $($s.state)"; return $s }
    if ($s -and $s.diag -and -not $s.diag.running) { return $s }
  }
  W "!! timeout"; Status
}

try {
  W ("=== calibration suite '{0}' on {1}, {2:o}" -f $Suite, $Port, (Get-Date))
  Drain | Out-Null
  $st = Status
  if (-not $st -or $st.state -ne 'CONFIG_AP') { Send "A" 8000 | Out-Null; $st = Status }
  if (-not $st -or $st.state -ne 'CONFIG_AP') { throw "the tracker did not enter CONFIG_AP" }
  W ("start: pos {0} enc {1} overshoot {2} slew default  vbat {3}" -f $st.pos_deg, $st.encoder_deg, $st.overshoot_deg, $st.battery_v)
  foreach ($step in $plan) {
    if ($step -like 'goto*') {
      $deg = [double]$step.Split(' ')[1]
      Send ('H POST /api/command {"cmd":"goto","deg":' + $deg + '}') | Out-Null
      $sw = [Diagnostics.Stopwatch]::StartNew()
      do { Start-Sleep -Seconds 4; $s = Status } while ($sw.Elapsed.TotalMinutes -lt 12 -and $s -and $s.state -eq 'CONFIG_AP' -and [math]::Abs($s.pos_deg - $deg) -gt 0.001 -and -not $s.move_failed)
      Start-Sleep -Seconds 2; $s = Status
      W ("GOTO {0}: pos {1:N4} enc {2:N4} ({3:N0} s){4}" -f $deg, $s.pos_deg, $s.encoder_deg, $sw.Elapsed.TotalSeconds, $(if ($s.move_failed) { "  MOVE FAILED guard=$($s.guard)" } else { "" }))
      continue
    }
    $r = Send ('H POST /api/command ' + $step)
    if ($r -notmatch 'HTTP 200') { W "!! refused: $step -> $r"; continue }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $s = WaitDiag 45
    $d = $s.diag
    W ("RESULT {0} at {1:N1} out {2} x{3} overshoot {4:N3} slew {5}: ok={6} ret_mean={7}`" sd={8}`" max={9}`" out_mean={10} deg rets=[{11}] outs=[{12}] ({13:N0} s){14}" -f `
       $d.kind, $d.base_deg, $d.out_deg, $d.cycles, $d.overshoot_deg, $(if ($d.slew_hz) { $d.slew_hz } else { "default" }), $d.ok,
       $d.return_mean_arcsec, $d.return_sd_arcsec, $d.return_max_arcsec, $d.out_mean_err_deg,
       ($d.return_err_arcsec -join ','), ($d.out_err_deg -join ','), $sw.Elapsed.TotalSeconds,
       $(if (-not $d.ok) { "  FAILED: $($d.msg) guard=$($s.guard)" } else { "" }))
  }
  $s = Status
  W ("=== end {0:o}: pos {1} enc {2} vbat {3}" -f (Get-Date), $s.pos_deg, $s.encoder_deg, $s.battery_v)
  W "summary lines: Select-String '$Out' -Pattern '^(RESULT|GOTO|!!)'"
} finally {
  $p.Close()
}
