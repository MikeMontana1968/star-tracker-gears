# RESUME — where this stands and what is next

Last touched 2026-10-01.

> **PCB FROZEN — Rev C Gerbers were sent to the fab house (2026-10-01).**
> Do NOT modify `hardware/star_tracker_ctrl/`, `design.py`, `gen_board.py`,
> `place.py`, `route.py` or re-export Gerbers. Work around it in the build
> instead. Known workaround: L1 is a Murata `19R333C` (6.0 mm lead pitch) on
> the 5.0 mm `Fastron_11P` footprint; squeeze the leads 1 mm. The footprint
> name is cosmetic. Any real board change means a new revision (Rev D).
> Mouser order files: `hardware/mouser_passives.csv`, `mouser_semiconductors.csv`.
> Backorder substitution: Panasonic EEU-FR1E221 (C5) and EEU-FR1E101 (C11) were
> backordered at Mouser, and so was the proposed Nichicon `UHE1E221MPD`
> (`hardware/mouser_backorder_replacements.csv` is NOT ordered). Bought on
> **Amazon** instead (2026-10-01): Innfeeltech 220 uF / 25 V / 105 C aluminum
> radial electrolytic, 50-pack, general-purpose (ESR not stated). Use for C5
> and C11. **C11 is therefore 220 uF, not 100 uF** (a fine upsize, more VMOT
> bulk). If the driver resets or steps noisily, swap C11 for a low-ESR part
> first. Bend leads out to the 5 mm pads. `mouser_passives.csv` still lists the
> original Panasonic parts.

## Controller board Rev C — sent to fab

The blocker is gone. The pin check ran on the module in hand and the board
was redrawn for it:

```
chip ESP32-D0WD-V3  rev 300  2 core(s)  flash 4 MB
PSRAM: none
I2C scan:
  GPIO21 (SDA) / GPIO22 (SCL)  <- ESP32 default
      0x3C  SSD1306 OLED
  GPIO4  (SDA) / GPIO15 (SCL)  nothing
  GPIO5  (SDA) / GPIO4  (SCL)  nothing
```

- The OLED shares the sensor I²C bus on 21/22; `WAKE` stays on GPIO 4.
- No PSRAM (WROOM-class), so GPIO 16/17 are free.
- `hardware/design.py` carries the 30-pin map (FIRMWARE.md §4 is the table),
  `REV = "C"`. The GPS UART nets are named by direction
  (`ESP_TX_GPS_RX`, `GPS_TX_ESP_RX`), fixing Rev B's TX-to-TX wiring.
- Generated, routed, poured: **ERC 0, DRC 0 unconnected / 0 clearance / 0
  hole-to-hole**; only cosmetic silkscreen overlaps. PCB.md §10.
- Router changes this round: drill-to-drill keepout for vias, a two-pass GND
  stitch, and `GND_RESCUE` (named pads escaped before any signal).

### Next

1. Look over `hardware/board_top.png` and `hardware/schematic.pdf`; export
   Gerbers (PCB.md §11) and order.
2. Firmware slice 1 onward (FIRMWARE.md §0). The pin constants are §4.
3. Build per the assembly guide, `docs/assembly_guide/stjarnspar.pdf`.
## Rev D layout notes (from building Rev C)

Collected while assembling the first Rev C board; apply together at the next
regenerate.

1. **F1 fuse clips are turned 90 degrees.** The footprint is
   `Fuseholder_Clip-5x20mm_Littelfuse_111_Inline`: its two legs per clip run
   along the fuse axis, but the clips in hand have their legs across it. Swap to
   a `..._Lateral_...` footprint (legs 5.00 mm apart across the fuse); pick it by
   the clip-to-clip spacing of the actual clips (Littelfuse 111 Lateral 18.80,
   Bel FC-203-22 17.80, Littelfuse 521 17.00, Schurter OG 15.00). Rev C build:
   hand-hacked.
2. **C11, C12, R7 and R9 crowd J4.** Too close to the motor screw terminal to get
   the motor leads in around them. Move the TMCX region in `place.py` (x 112-148,
   y 56-80) away from J4 at (118, 48) -- keep C11 at the driver's VM pin, which is
   its whole job, and give J4 clear access from the board edge.
3. **Label J2 on the silkscreen as camera power, not USB data.** It reads like a
   second USB port for the ESP32; it is the switched camera 5 V (`+5V_CAM`), with
   D+/D- tied together (charging-port signal) and no data. Something like
   `CAMERA 5V OUT - power only` beside J2 (and `CAM 5V` already marks J3), plus
   `PROGRAM VIA THE ESP32'S OWN USB` near the module's USB end. Add to `SILK` in
   `place.py`.
4. **Label SW1 on the silkscreen** -- `CONFIG / WAKE`. It reads as a reset
   button; reset is the ESP32 module's own EN button. SW1 wakes the tracker from
   deep sleep, raises the setup Wi-Fi, and steps the OLED pages.
5. **Room for clip-on heatsinks on Q1, Q2 and U2** (all TO-220). Keep the space
   around each tab clear of other parts, and keep the heatsinks from touching
   each other: the tabs are live -- Q1's at +12 V (drain), Q2's at +5V_CAM
   (drain), U2's at GND. By calculation Q1 and Q2 dissipate under 0.2 W and run
   cool bare, but Q2 sees only -5 V of gate drive (see the Test 4 note), so the
   space is cheap insurance.
   **Design target (no heatsinks bought yet):** a generic TO-220 clip-on or
   push-on heatsink, fins on the tab side. Reserve a keep-out per device of
   **22 mm wide x 16 mm deep behind the tab x 25 mm tall**, plus **2 mm air**
   between neighbouring keep-outs and to any tall part (electrolytics, L1). Draw
   it on F.Fab/F.Courtyard so `layout.check()` enforces it, and print the outline
   on the silkscreen. Then buy any clip-on TO-220 heatsink that fits inside it
   (roughly 20 x 13 mm footprint is the common size).
6. **A jumper table on the silkscreen**, beside JP3-JP5 and J5:

   | Jumper | Fit | What it does |
   |---|---|---|
   | JP4 MS1 | to GND | TMC2209 UART address bit 0 |
   | JP5 MS2 | to GND | address bit 1 -- both to GND = address 0, which the firmware expects |
   | JP3 PDN ALT | open | bridge only if your TMC2209 module puts UART on pin 5 |
   | J5 DIAG | wire | flying lead from the module's DIAG pad (StallGuard) |

   Silkscreen is small text: about 1.0 mm, four lines, in the clear area right
   of the jumpers. Also note there that the V<sub>REF</sub> pot is unused (UART sets
   current).
7. **Name the bottom-edge connectors**, above each one (pin names are already
   printed beside the pins):

   | Ref | Label |
   |---|---|
   | J7 | `AS5600 ENCODER` |
   | J6 | `DS3231 RTC - module plugs in` |
   | J8 | `HOME SENSOR (optional)` |
   | J10 | `I2C EXPANSION` |
   | J9 | `GPS - cable on 2-5, PPS lead on 1` |

   Place them in the fan-out channel above the strip; silk over copper there is
   cosmetic.
8. **Print each small transistor's part number beside its reference**:
   `Q3 2N3904`, `Q4 2N3906`, `Q5 2N3904`. The 2N3904 (NPN) and 2N3906 (PNP) look
   identical in TO-92 and swapping them fails quietly; the board should say
   which goes where. (Q1/Q2 IRF4905 too, while at it.)
9. **Separate the `Q4` reference from the `GPS PWR` group label** -- they print
   on top of each other. Move `GPS PWR` (a `SILK` entry in `place.py`, at
   90, 68) clear of the Q4/Q5/R17-R19 group, and check the DRC's silk-overlap
   list for any others touching a reference designator.
10. **Label J3 like J2**: `CAMERA 5V OUT - power only`, with `+5V` / `GND` at
    its pins (pin 1, the square pad, is +5V_CAM). J3 is the screw-terminal twin
    of the USB-A J2, both switched by Q2; the label keeps anyone from treating it
    as a supply input.
11. **Turn J1 so its wire openings face the board edge.** All three screw
    terminals (J1, J3, J4) use the MaiXu footprint at 0 deg, whose wire entry is
    the footprint's +y side (the line printed just past the holes). That suits
    J3 on the bottom edge, but J1 sits at the TOP edge (y 8), so its openings
    face into the board and the 12 V lead has to come in over the parts. Rotate
    J1 180 deg in `place.py` (pin 1, +12V_IN, then sits on the right: update
    the `+12V` / `GND` silk labels and the guide's J1 drawings to match). J4's
    openings face C11/C12 (note 2); check them against wherever TMCX moves.
12. **Put the revision and a contact on the silkscreen.** Today `design.REV`
    reaches only KiCad's title block, so nothing on the board itself says which
    revision it is. Add one line of board text built FROM `design.REV`, so it
    can never go stale, plus the owner's address:
    `STAR TRACKER CTRL  REV D  -  MikeMontana@Gmail.com` (or two lines if
    space is tight), size about 1.2 mm, in a clear area away from pads. Check
    the DRC silk-overlap list after regenerating.
13. **Add a ground-island check to the board build (2026-11-21).** First motor
    spin: no supply current, and the TMC2209's logic floated ~8 V above the
    12 V supply's minus, which read as 8-11 V on EN, VIO and VREF. Suspected a
    GND island at J31. The DESIGN IS FINE: a copper-connectivity check of the
    saved zone fills (fill polygons + tracks per layer, joined by vias and
    through-hole pads) puts all 54 GND pads in ONE piece, J1.2, J31.2/8 and
    C11.2 included, and kicad-cli DRC (as saved, and with --refill-zones)
    reports 0 unconnected. The fault was physical: the supply's minus not
    reaching board ground (the board was grounded only through USB). Keep the
    check for Rev D: run it after fill_zones as an island test, since "0
    unconnected" counts ratsnest, not copper pieces. Script kept at
    hardware/check_gnd_islands.py.
14. **Motor bring-up result (2026-11-21).** After the 12 V ground was fixed the
    motor held torque but would not step and the UART was silent: the TMC2209
    module (DIG TREE TECH v1.3, both pin-4/5 labelled "pdn") was not seated
    fully in its socket. Reseated, firmware/tmc_probe (read-only UART) reads
    version 0x21 at address 0, IOIN follows EN/STEP/DIR, MSCNT advances 128
    per 100 steps (1/8), DRV_STATUS no faults, CS_ACTUAL 31, StealthChop. JP3
    fitted (harmless on this module). VREF set 0.50 V (~350 mA). Motor turns
    both ways. **Direction observed (firmware/spin_dir, viewed top-down onto
    the plate, motor below): DIR LOW = motor CLOCKWISE, DIR HIGH = CCW.** Four
    single-mesh stages = even reversals, so the output follows the motor:
    **tracking (output CW from the camera end) = DIR LOW** (predicted; the
    AS5600 raw should count UP during the DIR LOW leg). **CONFIRMED by
    first_spin with the gear train in:** DIR HIGH -57 counts (-5.01 deg, CCW),
    DIR LOW +53 counts (+4.66 deg, CW) for 46161 steps each; 1/8 predicts
    5.07 deg. 1/8 microstepping confirmed; tracking DIR = LOW. The 4-count
    shortfall on the reversed leg (net -4) is backlash, ~0.35 deg at the
    output: take it up with a short pre-roll in the tracking direction
    before an exposure run.
    Bench sketches: first_spin, tmc_probe, driver_hold, driver_lines,
    driver_off, spin_dir.
15. **Firmware slice 1 (motion) done (2026-11-21).** PlatformIO project in
    firmware/ (lib/motion StepEngine + StepTimer, 11 host tests pass, bench
    console in src/main.cpp: s/g/t/r/x/z/v/e). Bench: 30x tracking exact,
    rewind with backlash overshoot arrives forward, no lost steps over ~500k
    pulses at 3200 p/s. AS5600 ~3% locally non-linear (magnet eccentricity)
    -- step count is the position. Details: FIRMWARE.md "Slice 1 as built".
    Next: slice 3 (TMC2209 UART -> MRES 1/32 for tracking, AS5600 map, RTC,
    OLED) or slice 2 (FSM + simulator, no hardware).
16. **Firmware slice 3 (hal) built (2026-11-22).** TMC2209 UART (1/8 slew,
    1/32 track), driver-reset watchdog, AS5600, DS3231 + alarm, deep sleep
    with RTC wake, battery, OLED, all on the bench console (i/T/A/S/b/k/n/c).
    **Keep the bench supply limit at >= 500 mA**: at 0.4 A every move start
    browned the TMC2209 out (it resets to 1/8). Encoder map swept and saved
    (map within +-12' of the step count); OLED confirmed; jam + slip guards
    added after real binds. Gear wobble (6.0 bores on #12 screws) currently
    tamed with spacers/washers/jam nuts -- 5.7 bore or bushings is the real
    fix. Camera/GPS switches metered (J3 5.0 V; J9.2 4.79 V loaded): slice 3
    complete. Details: FIRMWARE.md "Slice 3 as built".
17. **Firmware slice 2 (night FSM + simulator) done (2026-11-22), host only.**
    lib/app/Platform.h (interfaces) + Fsm; test/test_fsm_sim with 18 night
    scenarios, deep sleep simulated as a real reboot. 35 host tests total.
    Next: slice 4 (schedule + solar solver, host-tested), then an ESP32
    Platform so the FSM runs on the board.
18. **Firmware slice 4 (schedule + solar) done (2026-11-22), host only.**
    Solar solver within 2 s of Skyfield/DE421 at mid-latitudes; schedule JSON
    with 4-layer validation, twilight fallback, GPS site refresh. 46 host
    tests. Next: the ESP32 Platform (FSM on the board, schedule on LittleFS),
    then slice 5 (web API) and 6 (GoPro).
19. **The FSM runs on the board (2026-11-22).** src/EspPlatform + main.cpp
    (`pio run -e esp32-oled`); bench console is `-e esp32-bench`. A 10-min
    test night ran end to end on the hardware (tracked 05:35-05:45Z to 2.512
    deg, rewound, reported). Serial console `s n U<json> T<unix> r g B F Z`.
    **Stay-awake is ON by default (bench): `B` turns it off for deployment.**
    The board now holds the test schedule (one night, finished) and sleeps
    with a 6 h recheck. Next: slice 5 (WiFi AP + web API/UI), slice 6
    (GoPro), a real dusk-to-dawn night outdoors with GPS.
20. **Slice 5: Wi-Fi AP + web UI (2026-11-22).** Press the button (or `A` on
    the console) -> CONFIG_AP -> softAP `StarTracker-2E74`, password
    `AP_PASS` in config.h (**change it: the repo is public**), page at
    http://192.168.4.1/ (captive portal opens it). Status, halt / end night,
    jog / goto / home, test track, clear fault, set clock from the phone,
    schedule upload with the error list, night reports, live log. API in
    lib/net (host-tested, 64 host tests total); UI in firmware/web/index.html
    (`?mock` works offline). Verified on the board over serial (`H`), incl. a
    2-min test track. **Not yet verified: a phone actually joining the AP.**
    Lash note in FIRMWARE.md "Slice 5 as built". Next: slice 6 (GoPro).
21. **Diagnostics, calibration, Schedule Studio (2026-11-22).**
    - Web UI "Diagnostics" card (CONFIG_AP): wiggle, sweep, repeat test (optional
      overshoot / slew overrides), coils on/off. API `diag` / `coils` commands.
    - **Baseline calibration of today's #12-screw-post train:
      `docs/calibration/2026-11-22_baseline_screw_posts.md`.** Lash < 0.09 deg at
      park but ~0.75 deg at 47-48.5 deg (eccentric centres); 3200/s rewinds lose
      steps (+0.56 / +0.82 deg), 2000/s and 1200/s come home exact -> **SLEW_MAX_HZ
      lowered to 2000**, orient/rewind/home deadlines raised to 20 min.
    - **TODO when the 696ZZ + 6 mm shoulder-bolt build is in (parts due
      2026-12-10): run `firmware/tools/Run-Calibration.ps1` (suite all, ~75 min)
      and COMPARE against the baseline doc -- lash vs angle, rewind return
      error at 3200/s (can SLEW_MAX_HZ go back up?), sigma across runs, jams.
      The user asked for this comparison explicitly.**
    - Schedule Studio: `firmware/tools/Start-ScheduleStudio.cmd` -> USB bridge
      (tools/usb_bridge.py, pyserial) -> studio.html: compose / validate on the
      device / preview resolved nights / send / load / delete. New endpoints
      `POST /api/schedule/check`, `GET /api/schedule/nights`, `DELETE /api/schedule`.
      Verified end to end on the board. The bridge holds the COM port.
    - Fixed: map extrapolation past its ends (false "slip" at -1.44 deg);
      ENC_RAW_START 1029 -> 1059 (zero moved with the re-shim); the FSM held a
      stale schedule reference (uploads/deletes now take effect at once, a sleeping
      bench board re-plans); guard trips print their numbers; DRV_STATUS in status.
22. **Slice 6: GoPro HERO6 driver (2026-11-22)** -- built, host-tested (85 host
    tests), on the board; see FIRMWARE.md "Slice 6 as built". Verified without
    a camera: discover and a camera test track both fail cleanly (best effort:
    the test still tracked on time and reported camera=fail). The board has NO
    camera profile now. **Next, with the user and the HERO6:** Schedule Studio ->
    Camera profile: the user types the camera's Wi-Fi name + password + MAC
    (Connections > Camera Info; never typed by Claude) -> Save -> Discover ->
    check/fix every setting id against the dump -> Test track + camera
    (10 min) -> answers "does the AP stay rejoinable mid-record" and WoL.
23. **Rev D: J2's USB-A plug hits J11 (found 2026-11-22).** J2 (camera USB-A,
    `place.py` (60, 8, 0)) opens sideways along the top edge, and a plug plus
    its strain relief (~35-40 mm) runs into J11 (spare IO, (26, 7, 90)), with
    J1 and H1 next in line. Rotate J2 so the plug enters from the board edge
    (opening outward), and keep ~45 mm clear in front of any USB opening;
    re-check J1's turn (note 11) at the same time. Rev C build workaround: a
    90-deg "up angle" USB-A adapter, or feed the camera from J3 (same switched
    +5V_CAM) with a USB-C pigtail -- twist D+ and D- together in the pigtail,
    as J2's footprint does, so the GoPro charges at full current.
24. **GPS verified on the board; PPS dropped (2026-11-22).** New console
    `G on|off` (IDLE/CONFIG_AP/FAULT only) and `G raw [s]` (raw NMEA echo);
    `g` now shows bytes and UTC. GY-NEO6MV2 on UART1 9600: 0 bad checksums
    in 228 sentences; cold start indoors to a 3-D fix in ~2 min (7 sats,
    HDOP 1.5). **No PPS lead**: nothing in the firmware reads it. Time comes
    from the RMC (whole seconds; sentence latency well under 1 s, i.e.
    < 0.004 deg of sky), and the tracking rate comes from the ESP32 crystal
    through the step timer, not from absolute time -- so PPS would only have
    measured crystal ppm (`gps.pps_ppm`, never implemented). 20 ppm over a
    10 h night is 0.7 s = 0.003 deg, far below the gear lash. J9.1 and
    GPIO 23 stay unused.
25. **GoPro on the bench (2026-11-22): the camera is a HERO5 Black
    (HD5.02.02.70), not a HERO6.** Discover works; every id in
    web/camera_profile.example.json is verified against its dump (the HERO6
    guesses were wrong: protune 34 not 21, ISO 37/76 not 24, interval 3601 not
    0, recording key 10 `encoding_active`, not 8 `system_busy`). Findings, each
    fixed + host-tested:
    - **A slept HERO5 cannot be woken**: not by wake-on-LAN, not by cycling the
      USB rail. Asleep it still answers status with 200
      `{"state":{"shutdown_pending":true}}`. So: `sleep_after_stop` false,
      Auto Off = never (59=0), the user turns the camera on at setup; wake
      always sends WoL and needs two full status reads to call it awake.
    - **A recording HERO5 could not be rejoined** (20 s) to stop it: the radio
      table now keeps STA from the camera's start to its stop (beats the bench
      AP and D8; the USB bridge still reaches the tracker).
    - Stop must outlast the frame in progress: `timing.stop_s` (45 s), CAM_STOP
      90 s, SAFE_STOP's camera stop 60 s.
    - Bench supply >= 1.5 A with the camera on USB (it charges at ~1 A; a
      500 mA limit sagged 12 V to 5.4 V and safe-stopped "battery low").
    - Lines over ~1.5 KB on the USB console were corrupted: RX buffer 8 KB,
      and the bridge writes in paced 128-byte chunks.
    **Still unproven on hardware:** one camera test night with the stay-joined
    radio (the last two attempts could not find the camera's Wi-Fi -- check the
    camera is on with Wireless on, then `test_track {camera, preset}`).
    Tracking divergence reached 0.56 deg in one 10-min run (0.16-0.27 in the
    others) -- look at it before an unattended night.
26. **Workflows (2026-11-22): camera presets, Dark sky + Moon quick plans.**
    The camera profile has shared `settings` plus named `presets` (each night's
    `camera` picks one; unknown name = config fails loudly; the Studio warns).
    Schedule `target: "moon"` clips each night to the moon being up inside its
    start..stop window and tracks at the mean lunar rate (89428.33 s/rev;
    `TrackRate` in NightPlan, Persist magic FSM4). lib/app/Moon (Meeus ch. 47,
    truncated) matches Skyfield/DE421 to 19 s worst over 36 rise/sets at 3
    latitudes. Studio: "Dark sky" and "Moon" buttons (N nights from a date,
    with a preset), a Target selector, and a moon column (phase, rise bearing,
    end bearing/alt) for aiming. 94 host tests.

## Module facts, measured and confirmed

| | |
|---|---|
| Board | ideaspark ESP32 + 0.96" OLED, silkscreen `ESP32 OLED-0.96 V3.0` |
| Module | ESP32-D0WD-V3 rev 3, 4 MB flash, no PSRAM (pin check 2026-09-27) |
| Pins | **15 per side, 30 total** |
| Pitch | **2.54 mm** — 14 gaps measured end-to-end as 35.56 mm |
| Row spacing | **27.94 mm** (1.1 in) — inside 27.27 + outside 28.90 → 28.09 measured |
| USB | at the **bottom**; EN button bottom-left, BOOT bottom-right |

### Pin layout — read off the silkscreen and confirmed by the user

Standard DOIT DevKit V1 layout. Note **VIN is on the left column and 3V3 on
the right**, opposite ends, unlike the 38-pin board.

| # | LEFT (J2) top→bottom | RIGHT (J1) top→bottom |
|---|---|---|
| 1 | EN | D23 |
| 2 | VP (GPIO36) | D22 |
| 3 | VN (GPIO39) | TX0 (GPIO1) |
| 4 | D34 | RX0 (GPIO3) |
| 5 | D35 | D21 |
| 6 | D32 | D19 |
| 7 | D33 | D18 |
| 8 | D25 | D5 |
| 9 | D26 | TX2 (GPIO17) |
| 10 | D27 | RX2 (GPIO16) |
| 11 | D14 | D4 |
| 12 | D12 | D2 |
| 13 | D13 | D15 |
| 14 | GND | GND |
| 15 | VIN | 3V3 |

## Mechanical Rev B -- done 2026-09-27

Writing the assembly guide turned up eight conflicts in the Rev A CAD. All are
fixed in the `.scad` and the docs:

| # | Rev A problem | Rev B |
|---|---|---|
| 1 | Tower cone sat inside the plate; the flange could not seat | Halves clamp the plate: flange above, Ø41 disc below, dowels across |
| 2 | Output shaft ran through the 1/4"-20 nut | Shaft cut to 55 mm, z -33 to 22 |
| 3 | M3x10 motor screws bottom out | M3x8 flat head |
| 4 | Motor screw heads and one M5 sit under the S1 wheel (2 mm) | Countersink them by hand; the plate is already cut |
| 5 | Lower hub rubbed the tower rim and the bearing's outer race | Printed washer on the inner race, hub 1 mm shorter |
| 6 | AS5600 bracket had no axial stop | Bracket stops on the disc; gap set by the parts |
| 7 | Grub holes in gears that turn on fixed nails | Removed from `stage` and `final_stage_gear` |
| 8 | 1/4"-20 nut pocket 2.2 mm deep | Upper hub 10 mm thick, 5.9 mm pocket; wheel screws are M3x20 |

**2026-10-23: shafts 1-3 are #12 screws, not nails.** The nails did not work
out. The plate's 4.9 mm holes were drilled out to 5.5 by hand, and each screw is
nutted under the plate. Gears and spacers are rebored to 6.0 mm (`$Bore` in
`build.ps1`; #12 is 5.49 mm over the threads, plus the 0.5 mm rule). The gears
ride on the threads, so a screw with a smooth shank under the gear runs better.

## Still open

- **Is the OLED on the rotating camera platform or the fixed enclosure?** The
  camera sweeps ~180° a night and the enclosure does not, so "display opposite
  the lens" only holds at one point in the sweep if the panel is fixed.
  `oled_on_while_tracking` currently defaults to on.
- The three HERO6 unknowns in HARDWARE.md §6 are still unmeasured.

## Where things are

| | |
|---|---|
| `README.md` | project overview, written as a resume doc |
| `FIRMWARE.md` | firmware architecture, state machine, pin map, build order |
| `HARDWARE.md` | electronics architecture and BOM |
| `hardware/PCB.md` | board design notes, orientation rules, assembly order |
| `hardware/*.py` | the generator: design → place → layout → route → gen_board |

The KiCad project is **generated, not hand-drawn**. Edit `design.py` and
`place.py`; anything done in the GUI is lost on the next run.
