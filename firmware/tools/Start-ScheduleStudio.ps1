# Start the USB bridge and open the Schedule Studio in the browser.
#   .\Start-ScheduleStudio.ps1              (auto-detects the CH340 port)
#   .\Start-ScheduleStudio.ps1 -Port COM10
# Close the bridge window (or Ctrl+C in it) to free the COM port again --
# PlatformIO uploads and the serial monitor need it.
param([string]$Port = "", [int]$Http = 8780)
$ErrorActionPreference = "Stop"
$bridge = Join-Path $PSScriptRoot "usb_bridge.py"
python -c "import serial" 2>$null
if ($LASTEXITCODE -ne 0) {
  Write-Host "Installing pyserial..."
  python -m pip install --user pyserial
}
$pyArgs = @("`"$bridge`"", "--http", $Http)
if ($Port) { $pyArgs += @("--port", $Port) }
Start-Process python -ArgumentList $pyArgs -WindowStyle Normal
