#requires -Version 5.1
<#
.SYNOPSIS
    Regenerate every build artifact for the sidereal drive gearbox.

.DESCRIPTION
    One source of truth: star_tracker_gears.scad. Everything under out/ is
    generated from it. This script carries the exact -D flags for each part,
    including the printer calibration constants measured on this machine.

.EXAMPLE
    .\build.ps1                     # everything
    .\build.ps1 -Group gears        # just the parts you print
    .\build.ps1 -Only pinion.stl    # one target
    .\build.ps1 -List               # show targets without building
#>
[CmdletBinding()]
param(
    [ValidateSet('all', 'gears', 'encoder', 'calibration', 'docs')]
    [string] $Group = 'all',
    [string] $Only,
    [switch] $List,
    [string] $OpenScad = 'C:\Program Files\OpenSCAD\openscad.exe'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$scad = Join-Path $root 'star_tracker_gears.scad'
$out  = Join-Path $root 'out'

# ---------------------------------------------------------------------------
#  PRINTER CALIBRATION — measured, not guessed. See PRINTING.md.
# ---------------------------------------------------------------------------
# A printed post must run ~0.50 mm under a printed hole's nominal diameter for
# a snug rotating fit on this machine (hole shrink + post growth combined).
$Bore      = 5.20   # gears 1-4, running on 4.88 mm 20d common nails
$MotorBore = 5.45   # motor pinion, on the NEMA17's 5.00 mm ground shaft
$MotorFlat = 2.225  # D-cut flat: 2.0 + (MotorBore - 5.0) / 2
$PostD     = 4.70   # test-plate posts, from FITGAUGE
$BrgFit    = 0.50   # 625ZZ pocket allowance -- SET FROM THE BEARING GAUGE
$OutBolts  = 6      # output wheel clamp bolts
$OutBoltR  = 18     # ...on this radius, clear of the 22 mm register
$PlateT    = 4.3    # baseplate thickness, mm (as ordered)

# ---------------------------------------------------------------------------
#  TARGETS
# ---------------------------------------------------------------------------
$targets = @(
  # --- the five pieces you actually print -------------------------------
  @{ g='gears'; f='pinion.stl';                     p='pinion';
     d=@("mount_type=\`"dshaft\`"", "bore=$MotorBore", "d_flat=$MotorFlat") }
  @{ g='gears'; f='pinion_bore5.30.stl';            p='pinion';
     d=@("mount_type=\`"dshaft\`"", 'bore=5.30', 'd_flat=2.15') }
  @{ g='gears'; f='pinion_bore5.60.stl';            p='pinion';
     d=@("mount_type=\`"dshaft\`"", 'bore=5.60', 'd_flat=2.30') }
  @{ g='gears'; f='stage.stl';                      p='stage';      d=@("bore=$Bore") }
  @{ g='gears'; f='final_stage_gear.stl';           p='finalstage'; d=@("bore=$Bore") }
  @{ g='gears'; f='final_wheel_96T_m2_PRINTED.stl'; p='finalwheel';
     d=@('bore=22', "bc_holes=$OutBolts", "bc_r=$OutBoltR") }

  # --- output shaft + AS5600 encoder ------------------------------------
  # The one-piece tower does not print -- see PRINTING.md. Kept as a target
  # only for rendering and reference; build the two halves.
  @{ g='encoder'; f='_scratch/output_bearing_tower_ONEPIECE.stl'; p='tower';
     d=@("os_brg_fit=$BrgFit") }
  @{ g='encoder'; f='output_tower_LOWER.stl';  p='towerlower';
     d=@("os_brg_fit=$BrgFit") }
  @{ g='encoder'; f='output_tower_UPPER.stl';  p='towerupper';
     d=@("os_brg_fit=$BrgFit") }
  @{ g='encoder'; f='output_tower_DOWELS.stl'; p='towerdowels'; d=@() }
  @{ g='encoder'; f='output_hub_lower.stl'; p='hublower';
     d=@("bc_holes=$OutBolts", "bc_r=$OutBoltR") }
  @{ g='encoder'; f='output_hub_upper.stl'; p='hubupper';
     d=@("bc_holes=$OutBolts", "bc_r=$OutBoltR") }
  @{ g='encoder'; f='magnet_cap.stl';     p='magnetcap';     d=@() }
  @{ g='gears';   f='baseplate_4mm.stl';  p='baseplate';     d=@("bp_t=$PlateT") }
  @{ g='docs';    f='docs/baseplate_cut.svg'; p='baseplatecut'; d=@() }
  @{ g='encoder'; f='as5600_bracket.stl'; p='as5600bracket'; d=@() }
  @{ g='gears'; f='SPACERS_shaft_0-4.stl';          p='spacers';    d=@("bore=$Bore") }

  # --- calibration and test rig, into _scratch --------------------------
  @{ g='calibration'; f='_scratch/BOREGAUGE_hole_diameters.stl'; p='boregauge'; d=@() }
  @{ g='calibration'; f='_scratch/FITGAUGE_post_diameters.stl';  p='fitgauge';  d=@() }
  @{ g='calibration'; f='_scratch/TESTPLATE_15T_30T.stl';        p='testplate';
     d=@("tp_post_d=$PostD") }
  @{ g='calibration'; f='_scratch/TEST_wheel_30T.stl';           p='testwheel';
     d=@('zw=30', "bore=$Bore") }
  @{ g='calibration'; f='_scratch/BEARINGGAUGE_pockets.stl';     p='bearinggauge'; d=@() }

  # --- documentation ----------------------------------------------------
  # Laser profile: high flank resolution and a looser backlash, because a
  # cutting service gives you no second try.
  @{ g='docs'; f='docs/final_wheel_96T_m2_laser.svg'; p='lasercut';
     d=@('flank_pts=60', 'backlash=0.35') }
  @{ g='docs'; f='docs/assembly.png';          p='assembly'; d=@()
     img='1800,1350'; cam='0,0,0,58,0,22,0' }
  @{ g='docs'; f='docs/assembly_exploded.png'; p='assembly'; d=@('explode=45')
     img='1800,1350'; cam='0,0,0,68,0,22,0' }
  @{ g='docs'; f='docs/assembly_top.png';      p='assembly'; d=@()
     img='1500,1500'; cam='0,0,0,0,0,0,0' }
)

if ($List) {
    $targets | ForEach-Object { "{0,-12} {1}" -f $_.g, $_.f }
    "{0,-12} {1}" -f 'docs', 'docs/baseplate_template.pdf  (make_baseplate_pdf.py)'
    "{0,-12} {1}" -f 'docs', 'docs/baseplate_cut.dxf       (make_baseplate_dxf.py)'
    return
}

if (-not (Test-Path $OpenScad)) {
    throw "OpenSCAD not found at '$OpenScad'. Pass -OpenScad <path>."
}

$selected = $targets | Where-Object {
    ($Group -eq 'all' -or $_.g -eq $Group) -and
    (-not $Only -or $_.f -like "*$Only*")
}
if (-not $selected) { throw "No targets matched Group='$Group' Only='$Only'." }

$fail = 0
foreach ($t in $selected) {
    $dst = Join-Path $out $t.f
    $dir = Split-Path -Parent $dst
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

    $args = @('-o', $dst, '-D', "part=\`"$($t.p)\`"")
    foreach ($d in $t.d) { $args += @('-D', $d) }
    if ($t.img) {
        $args += @("--imgsize=$($t.img)", "--camera=$($t.cam)",
                   '--autocenter', '--viewall', '--colorscheme=Tomorrow')
    }
    $args += $scad

    Write-Host ("-> {0}" -f $t.f) -ForegroundColor Cyan
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $log = (& $OpenScad @args 2>&1 | Out-String)
    $ErrorActionPreference = $prev
    if ($LASTEXITCODE -ne 0) {
        Write-Host "   FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
        Write-Host $log
        $fail++
        continue
    }
    # A watertight solid reports "Simple: yes". Meshes only.
    if ($dst -like '*.stl') {
        if ($log -match 'Simple:\s+yes') {
            $v = if ($log -match 'Volumes:\s+(\d+)') { $Matches[1] } else { '?' }
            Write-Host "   ok  manifold, $v volumes" -ForegroundColor Green
        } else {
            Write-Host "   WARNING: not reported manifold" -ForegroundColor Yellow
            $fail++
        }
    } else {
        Write-Host "   ok" -ForegroundColor Green
    }
}

# ---- the 1:1 drilling template ---------------------------------------------
if ($Group -eq 'all' -or $Group -eq 'docs') {
    if (-not $Only -or 'baseplate_template.pdf' -like "*$Only*") {
        Write-Host '-> docs/baseplate_template.pdf + baseplate_cut.dxf' -ForegroundColor Cyan
        Push-Location $root
        try {
            python (Join-Path $root 'make_baseplate_pdf.py') | Out-Null
            python (Join-Path $root 'make_baseplate_dxf.py') | Out-Null
            if ($LASTEXITCODE -eq 0) {
                Write-Host '   ok' -ForegroundColor Green
            } else {
                Write-Host '   FAILED' -ForegroundColor Red; $fail++
            }
        } finally { Pop-Location }
    }
}

if ($fail -gt 0) {
    Write-Host "`n$fail target(s) failed." -ForegroundColor Red
    exit 1
}
Write-Host "`nAll targets built." -ForegroundColor Green
