"""Star tracker controller board — the netlist, the BOM and the placement.

Single source of truth. gen_board.py turns this into a KiCad project.

Everything a future change is likely to touch lives at the top:
the ESP32 pin map, the module row spacings, and the board size.
"""

# --------------------------------------------------------------------------
# Board
# --------------------------------------------------------------------------
BOARD_W = 150.0          # 5.91 in
BOARD_H = 100.0          # 3.94 in
EDGE_CLEAR = 0.5
MOUNT_INSET = 4.0

TITLE = "Star Tracker Controller"
REV = "A"
COMPANY = "star-tracker-gears"

# --------------------------------------------------------------------------
# Module geometry  -- VERIFY THESE AGAINST YOUR ACTUAL BOARDS BEFORE ORDERING
# --------------------------------------------------------------------------
# Row spacing between the two ESP32 header rows, centre to centre.
# 25.4 = the usual 38-pin ESP32-DevKitC / DevKit-V1 clone.
# Some "wide" 38-pin boards are 27.94 (1.1 in).  One number to change.
# ideaspark ESP32 + 0.96" OLED, 30-pin.  Measured on the actual module:
#   pitch        2.54 mm  (14 gaps measured as 35.56 mm end to end)
#   row spacing  27.94 mm (1.1 in); inside 27.27 + outside 28.90 -> 28.09
ESP32_ROW_SPACING = 27.94
ESP32_PINS_PER_SIDE = 15

# Pololu / StepStick standard.  Not vendor-dependent.
TMC_ROW_SPACING = 15.24

# ESP32-DevKitC 38-pin map, left header top->bottom then right header
# top->bottom, with the USB connector at the BOTTOM of the module.
# "-" means leave unconnected (flash pins, strapping pins, USB serial).
ESP32_LEFT = [
    ("3V3",    "+3V3"),
    ("EN",     "-"),
    ("IO36",   "SPARE36"),    # input-only
    ("IO39",   "TMC_DIAG"),   # StallGuard -> driver side
    ("IO34",   "VBAT_SENSE"), # divider sits directly above this pin
    ("IO35",   "-"),          # input-only, free
    ("IO32",   "CAM_EN"),     # camera switch is above this row
    ("IO33",   "SPARE33"),
    ("IO25",   "STEP"),       # driver side
    ("IO26",   "MOT_DIR"),    # driver side
    ("IO27",   "TMC_EN"),     # driver side
    ("IO14",   "SPARE14"),
    ("IO12",   "-"),          # strapping, must be low at boot
    ("GND",    "GND"),
    ("IO13",   "SPARE13"),
    ("IO9",    "-"),          # flash
    ("IO10",   "-"),          # flash
    ("IO11",   "-"),          # flash
    ("5V",     "+5V"),
]
ESP32_RIGHT = [
    ("GND",    "GND"),
    ("IO23",   "PPS"),        # v2 GPS pulse-per-second
    ("IO22",   "SCL"),
    ("IO1/TX", "-"),          # USB serial, keep free
    ("IO3/RX", "-"),          # USB serial, keep free
    ("IO21",   "SDA"),
    ("GND",    "GND"),
    ("IO19",   "HOME"),
    ("IO18",   "GPS_RX"),     # v2
    ("IO5",    "GPS_TX"),     # v2; strapping, idles high as UART TX
    ("IO17",   "ESP_TX"),     # -> 1k -> TMC PDN_UART
    ("IO16",   "TMC_UART"),   # RX straight onto PDN_UART
    ("IO4",    "WAKE"),       # DS3231 INT + button, wired-OR; RTC-capable
    ("IO0",    "-"),          # strapping
    ("IO2",    "GPS_EN"),     # module LED doubles as a GPS-power tell-tale
    ("IO15",   "LED_ST"),     # strapping; LED is high-Z at boot
    ("IO8",    "-"),          # flash
    ("IO7",    "-"),          # flash
    ("IO6",    "-"),          # flash
]

# TMC2209 SilentStepStick / BTT, 2x8 StepStick footprint.
# DIAG and INDEX are NOT on this header on any vendor's module -- they come
# out on separate pads, hence J5.
TMC_LEFT = [
    ("EN",       "TMC_EN"),
    ("MS1",      "MS1"),
    ("MS2",      "MS2"),
    ("PDN_UART", "TMC_UART"),
    ("PDN_ALT",  "PDN_ALT"),   # some modules put UART here instead; JP3 bridges
    ("CLK",      "-"),
    ("STEP",     "STEP"),
    ("DIR",      "MOT_DIR"),
]
TMC_RIGHT = [
    ("VM",   "+12V"),
    ("GND",  "GND"),
    ("A2",   "MOT_A2"),
    ("A1",   "MOT_A1"),
    ("B1",   "MOT_B1"),
    ("B2",   "MOT_B2"),
    ("VIO",  "+3V3"),
    ("GND",  "GND"),
]

# --------------------------------------------------------------------------
# Footprint shorthands
# --------------------------------------------------------------------------
FP_R      = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical"
FP_C      = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm"
FP_CP10   = "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm"
FP_CP8    = "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm"
FP_LED    = "LED_THT:LED_D3.0mm"
FP_TO220  = "Package_TO_SOT_THT:TO-220-3_Vertical"
FP_TO220_5 = "Package_TO_SOT_THT:TO-220-5_P3.4x3.7mm_StaggerOdd_Lead3.8mm_Vertical"
FP_TO92   = "Package_TO_SOT_THT:TO-92_Inline_Wide"
FP_DO41   = "Diode_THT:D_DO-41_SOD81_P2.54mm_Vertical_CathodeUp"
FP_DO15   = "Diode_THT:D_DO-15_P3.81mm_Vertical_AnodeUp"
FP_DO201  = "Diode_THT:D_DO-201AD_P5.08mm_Vertical_CathodeUp"
FP_IND    = "Inductor_THT:L_Radial_D12.0mm_P5.00mm_Fastron_11P"
FP_FUSE   = ("Fuse:Fuseholder_Clip-5x20mm_Littelfuse_111_Inline"
             "_P20.00x5.00mm_D1.05mm_Horizontal")
FP_TB2    = "TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm"
FP_TB4    = "TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-04P_1x04_P5.00mm"
FP_USBA   = "Connector_USB:USB_A_Wuerth_614004134726_Horizontal"
FP_BTN    = "Button_Switch_THT:SW_PUSH_6mm"
FP_R78    = "Converter_DCDC:Converter_DCDC_RECOM_R-78B-2.0_THT"
FP_MOUNT  = "MountingHole:MountingHole_3.2mm_M3"


def sock(n):
    return "Connector_PinSocket_2.54mm:PinSocket_1x%02d_P2.54mm_Vertical" % n


def hdr(n):
    return "Connector_PinHeader_2.54mm:PinHeader_1x%02d_P2.54mm_Vertical" % n


# --------------------------------------------------------------------------
# Parts
#   ref, value, symbol, footprint, {pin: net}, pcb(x, y, rot), sch(x, y)
#   net "-" = deliberately not connected
# --------------------------------------------------------------------------
P = []


def part(ref, value, sym, fp, pins, pcb, sch, dnp=False, note="",
         labels=None):
    P.append(dict(ref=ref, value=value, sym=sym, fp=fp, pins=pins,
                  pcb=pcb, sch=sch, dnp=dnp, note=note, labels=labels))


# ---- 12 V input, fusing, reverse-polarity, transient clamp ---------------
part("J1", "12V IN", "Connector_Generic:Conn_01x02", FP_TB2,
     {"1": "+12V_IN", "2": "GND"}, (8, 10, 0), (30, 40),
     note="12-14.6V LiFePO4 in", labels=["+12V", "GND"])
part("F1", "3A", "Device:Fuse", FP_FUSE,
     {"1": "+12V_IN", "2": "+12V_F"}, (8, 22, 0), (30, 70))
part("Q1", "IRF4905", "Transistor_FET:IRF4905", FP_TO220,
     {"1": "GATE_RP", "2": "+12V_F", "3": "+12V"}, (9, 32, 0), (30, 100),
     note="reverse-polarity; drain to source of supply")
part("R1", "10k", "Device:R", FP_R,
     {"1": "GATE_RP", "2": "GND"}, (20, 36, 0), (75, 100))
part("D1", "1N4744A 15V", "Device:D_Zener", FP_DO41,
     {"1": "+12V", "2": "GATE_RP"}, (20, 28, 0), (75, 70),
     note="Vgs clamp, cathode to source")
part("D2", "P6KE20CA", "Device:D_TVS", FP_DO15,
     {"1": "+12V", "2": "GND"}, (35, 26, 90), (75, 40))
part("C1", "470uF/25V", "Device:C_Polarized", FP_CP10,
     {"1": "+12V", "2": "GND"}, (26, 34, 0), (115, 40))
part("C2", "100nF", "Device:C", FP_C,
     {"1": "+12V", "2": "GND"}, (34, 34, 0), (115, 70))

# ---- 12 V -> 5 V 3 A buck (LM2596T-5.0) ---------------------------------
part("U2", "LM2596T-5.0", "Regulator_Switching:LM2596T-5", FP_TO220_5,
     {"1": "+12V", "2": "SW_NODE", "3": "GND", "4": "+5V", "5": "GND"},
     (9, 47, 0), (170, 55), note="TO-220-5; needs a clip heatsink above ~1.5A")
part("C3", "470uF/25V LowESR", "Device:C_Polarized", FP_CP10,
     {"1": "+12V", "2": "GND"}, (20, 42, 0), (135, 40))
part("C4", "100nF", "Device:C", FP_C,
     {"1": "+12V", "2": "GND"}, (20, 56, 0), (135, 70))
part("L1", "33uH 3A", "Device:L", FP_IND,
     {"1": "SW_NODE", "2": "+5V"}, (30, 47, 0), (215, 40))
part("D3", "1N5822", "Device:D_Schottky", FP_DO201,
     {"1": "SW_NODE", "2": "GND"}, (16, 62, 0), (215, 70),
     note="catch diode, cathode to SW node")
part("C5", "220uF/25V LowESR", "Device:C_Polarized", FP_CP10,
     {"1": "+5V", "2": "GND"}, (40, 42, 0), (255, 40))
part("C6", "100nF", "Device:C", FP_C,
     {"1": "+5V", "2": "GND"}, (40, 56, 0), (255, 70))
part("R2", "1k", "Device:R", FP_R,
     {"1": "+5V", "2": "LED1_A"}, (47, 40, 90), (295, 40))
part("LED1", "GRN 5V", "Device:LED", FP_LED,
     {"1": "GND", "2": "LED1_A"}, (47, 52, 90), (295, 70))

# ---- camera 5 V high-side switch ----------------------------------------
part("Q2", "IRF4905", "Transistor_FET:IRF4905", FP_TO220,
     {"1": "GATE_CAM", "2": "+5V_CAM", "3": "+5V"}, (53, 20, 0), (330, 40),
     note="high-side switch, source to +5V")
part("R3", "10k", "Device:R", FP_R,
     {"1": "+5V", "2": "GATE_CAM"}, (53, 27, 0), (375, 40),
     note="default-off gate pull-up")
part("Q3", "2N3904", "Transistor_BJT:2N3904", FP_TO92,
     {"1": "GND", "2": "Q3_B", "3": "GATE_CAM"}, (53, 33, 0), (330, 75))
part("R4", "10k", "Device:R", FP_R,
     {"1": "CAM_EN", "2": "Q3_B"}, (62, 33, 0), (375, 75))
part("R5", "100k", "Device:R", FP_R,
     {"1": "Q3_B", "2": "GND"}, (53, 39, 0), (375, 105),
     note="holds camera off while GPIO floats")
part("C7", "100uF/16V", "Device:C_Polarized", FP_CP8,
     {"1": "+5V_CAM", "2": "GND"}, (63, 22, 0), (420, 40))
part("C8", "100nF", "Device:C", FP_C,
     {"1": "+5V_CAM", "2": "GND"}, (63, 27, 0), (420, 70))
part("R6", "1k", "Device:R", FP_R,
     {"1": "+5V_CAM", "2": "LED2_A"}, (46, 62, 0), (455, 40))
part("LED2", "GRN CAM", "Device:LED", FP_LED,
     {"1": "GND", "2": "LED2_A"}, (58, 62, 90), (455, 70))
part("J2", "CAM USB-A", "Connector:USB_A", FP_USBA,
     {"1": "+5V_CAM", "2": "USB_D", "3": "USB_D", "4": "GND", "SH": "GND"},
     (52, 8, 0), (330, 140), note="D+/D- shorted = dedicated charging port")
part("J3", "CAM 5V", "Connector_Generic:Conn_01x02", FP_TB2,
     {"1": "+5V_CAM", "2": "GND"}, (63, 38, 0), (420, 140),
     labels=["+5V", "GND"])

# ---- GPS power switch (5 V high side, off by default) -------------------
part("Q4", "2N3906", "Transistor_BJT:2N3906", FP_TO92,
     {"1": "+5V", "2": "GPS_B", "3": "GPS_VCC"}, (0, 0, 0), (330, 300),
     note="PNP high-side switch; the module's own LDO makes 3.3V from this")
part("R16", "10k", "Device:R", FP_R,
     {"1": "+5V", "2": "GPS_B"}, (0, 0, 0), (375, 300),
     note="holds Q4 off by default")
part("R17", "1k", "Device:R", FP_R,
     {"1": "GPS_B", "2": "GPS_C"}, (0, 0, 0), (420, 300))
part("Q5", "2N3904", "Transistor_BJT:2N3904", FP_TO92,
     {"1": "GND", "2": "GPS_EN_B", "3": "GPS_C"}, (0, 0, 0), (330, 340))
part("R18", "10k", "Device:R", FP_R,
     {"1": "GPS_EN", "2": "GPS_EN_B"}, (0, 0, 0), (375, 340))
part("R19", "100k", "Device:R", FP_R,
     {"1": "GPS_EN_B", "2": "GND"}, (0, 0, 0), (420, 340),
     note="GPS stays off while GPIO2 floats at boot")
part("C17", "100nF", "Device:C", FP_C,
     {"1": "GPS_VCC", "2": "GND"}, (0, 0, 0), (465, 300))

# ---- ESP32 devkit socket -------------------------------------------------
ESP_X = 70.0
ESP_Y = 13.0
part("J20", "ESP32 L", "Connector_Generic:Conn_01x%02d" % ESP32_PINS_PER_SIDE,
     sock(ESP32_PINS_PER_SIDE),
     {str(i + 1): n for i, (_, n) in enumerate(ESP32_LEFT)},
     (ESP_X, ESP_Y, 0), (520, 30))
part("J21", "ESP32 R", "Connector_Generic:Conn_01x%02d" % ESP32_PINS_PER_SIDE,
     sock(ESP32_PINS_PER_SIDE),
     {str(i + 1): n for i, (_, n) in enumerate(ESP32_RIGHT)},
     (ESP_X + ESP32_ROW_SPACING, ESP_Y, 0), (600, 30))
part("C9", "100uF/10V", "Device:C_Polarized", FP_CP8,
     {"1": "+3V3", "2": "GND"}, (80, 62, 0), (520, 160))
part("C10", "100nF", "Device:C", FP_C,
     {"1": "+3V3", "2": "GND"}, (87, 62, 0), (560, 160))
part("R8", "330R", "Device:R", FP_R,
     {"1": "LED_ST", "2": "LED3_A"}, (78, 7, 0), (600, 160))
part("LED3", "RED ST", "Device:LED", FP_LED,
     {"1": "GND", "2": "LED3_A"}, (90, 7, 90), (640, 160))
part("SW1", "CONFIG", "Switch:SW_Push", FP_BTN,
     {"1": "WAKE", "2": "GND"}, (96, 7, 0), (680, 160),
     note="also wakes from deep sleep via the GPIO4 wired-OR")

# ---- TMC2209 socket ------------------------------------------------------
TMC_X = 103.0
TMC_Y = 14.0
part("J30", "TMC2209 L", "Connector_Generic:Conn_01x08", sock(8),
     {str(i + 1): n for i, (_, n) in enumerate(TMC_LEFT)},
     (TMC_X, TMC_Y, 0), (700, 30))
part("J31", "TMC2209 R", "Connector_Generic:Conn_01x08", sock(8),
     {str(i + 1): n for i, (_, n) in enumerate(TMC_RIGHT)},
     (TMC_X + TMC_ROW_SPACING, TMC_Y, 0), (760, 30))
part("C11", "100uF/25V LowESR", "Device:C_Polarized", FP_CP10,
     {"1": "+12V", "2": "GND"}, (105, 38, 0), (700, 110),
     note="VMOT bulk - MUST sit at the driver socket")
part("C12", "100nF", "Device:C", FP_C,
     {"1": "+12V", "2": "GND"}, (114, 38, 0), (740, 110))
part("R7", "1k", "Device:R", FP_R,
     {"1": "ESP_TX", "2": "TMC_UART"}, (96, 38, 0), (700, 150),
     note="single-wire UART series resistor")
part("R9", "10k", "Device:R", FP_R,
     {"1": "TMC_DIAG", "2": "GND"}, (96, 44, 0), (740, 150))
part("J4", "MOTOR", "Connector_Generic:Conn_01x04", FP_TB4,
     {"1": "MOT_A1", "2": "MOT_A2", "3": "MOT_B1", "4": "MOT_B2"},
     (104, 50, 0), (760, 110), note="pins 1-2 one coil, 3-4 the other",
     labels=["A1", "A2", "B1", "B2"])
part("J5", "DIAG", "Connector_Generic:Conn_01x02", hdr(2),
     {"1": "TMC_DIAG", "2": "GND"}, (120, 44, 90), (700, 190),
     labels=["DIAG", "GND"])
part("JP3", "PDN ALT", "Connector_Generic:Conn_01x02", hdr(2),
     {"1": "TMC_UART", "2": "PDN_ALT"}, (120, 38, 90), (760, 190))
part("JP4", "MS1", "Connector_Generic:Conn_01x03", hdr(3),
     {"1": "+3V3", "2": "MS1", "3": "GND"}, (96, 50, 90), (700, 225),
     labels=["3V3", "MS1", "GND"])
part("JP5", "MS2", "Connector_Generic:Conn_01x03", hdr(3),
     {"1": "+3V3", "2": "MS2", "3": "GND"}, (96, 56, 90), (760, 225),
     labels=["3V3", "MS2", "GND"])

# ---- I2C, wake, sense ----------------------------------------------------
part("R10", "4.7k", "Device:R", FP_R, {"1": "+3V3", "2": "SDA"},
     (40, 68, 0), (110, 200), dnp=True,
     note="omit if the DS3231 module already has pull-ups (most do)")
part("R11", "4.7k", "Device:R", FP_R, {"1": "+3V3", "2": "SCL"},
     (40, 73, 0), (150, 200), dnp=True, note="see R10")
part("R12", "4.7k", "Device:R", FP_R, {"1": "+3V3", "2": "WAKE"},
     (55, 51, 0), (190, 200), note="the wired-OR pull-up; always populate")
part("C13", "100nF", "Device:C", FP_C, {"1": "WAKE", "2": "GND"},
     (66, 66, 0), (230, 200))
part("R13", "10k", "Device:R", FP_R, {"1": "+3V3", "2": "HOME"},
     (55, 56, 0), (270, 200))
part("R14", "100k 1%", "Device:R", FP_R, {"1": "+12V", "2": "VBAT_SENSE"},
     (110, 62, 0), (310, 200))
part("R15", "18k 1%", "Device:R", FP_R, {"1": "VBAT_SENSE", "2": "GND"},
     (110, 68, 0), (350, 200), note="14.6V -> 2.23V at GPIO34")
part("C16", "100nF", "Device:C", FP_C, {"1": "VBAT_SENSE", "2": "GND"},
     (110, 73, 0), (390, 200))
part("C15", "100nF", "Device:C", FP_C, {"1": "+3V3", "2": "GND"},
     (46, 56, 0), (430, 200))

# ---- peripheral connectors, bottom strip --------------------------------
part("J6", "DS3231", "Connector_Generic:Conn_01x06", sock(6),
     {"1": "-", "2": "WAKE", "3": "SCL", "4": "SDA", "5": "+3V3", "6": "GND"},     (24, 71, 90), (110, 260), note="ZS-042 order: 32K SQW SCL SDA VCC GND",
     labels=["32K", "SQW", "SCL", "SDA", "VCC", "GND"])
part("J7", "AS5600", "Connector_Generic:Conn_01x05", hdr(5),
     {"1": "GND", "2": "+3V3", "3": "SDA", "4": "SCL", "5": "GND"},     (60, 71, 90), (190, 260), note="remote on the encoder bracket; pin5 = DIR->GND",
     labels=["GND", "3V3", "SDA", "SCL", "DIR"])
part("J8", "HOME", "Connector_Generic:Conn_01x04", hdr(4),
     {"1": "GND", "2": "HOME", "3": "+3V3", "4": "GND"},
     (74, 71, 90), (270, 260),
     note="optional opto flag; GND on BOTH end pins so the pour can always "
          "reach one of them",
     labels=["GND", "SIG", "3V3", "GND"])
part("J9", "GPS", "Connector_Generic:Conn_01x05", hdr(5),
     {"1": "PPS", "2": "GPS_VCC", "3": "GPS_RX", "4": "GPS_TX", "5": "GND"},
     (82, 71, 90), (350, 260),
     note="GY-NEO6MV2: pins 2-5 match its VCC/RX/TX/GND cable; PPS is a "
          "flying lead from the module's PPS LED pad",
     labels=["PPS", "VCC", "RXD", "TXD", "GND"])
part("J10", "I2C EXP", "Connector_Generic:Conn_01x04", hdr(4),
     {"1": "GND", "2": "+3V3", "3": "SDA", "4": "SCL"},     (96, 71, 90), (430, 260), note="v2 compass",
     labels=["GND", "3V3", "SDA", "SCL"])
part("J11", "SPARE IO", "Connector_Generic:Conn_01x06", hdr(6),
     {"1": "GND", "2": "+3V3", "3": "SPARE33", "4": "SPARE14",
      "5": "SPARE13", "6": "SPARE36"},     (6, 71, 90), (510, 260),
     labels=["GND", "3V3", "IO33", "IO14", "IO13", "IO36"])

# ---- ERC power flags (schematic only, no footprint) ---------------------
for i, (flag_net, fx_, fy_) in enumerate([("+12V", 115, 110),
                                          ("GND", 155, 110),
                                          ("+5V_CAM", 195, 110)]):
    part("#FLG%d" % i, "PWR_FLAG", "power:PWR_FLAG", None,
         {"1": flag_net}, (0, 0, 0), (fx_, fy_))

# ---- mechanical ----------------------------------------------------------
for i, (mx, my) in enumerate([
        (MOUNT_INSET, MOUNT_INSET),
        (BOARD_W - MOUNT_INSET, MOUNT_INSET),
        (MOUNT_INSET, BOARD_H - MOUNT_INSET),
        (BOARD_W - MOUNT_INSET, BOARD_H - MOUNT_INSET)]):
    part("H%d" % (i + 1), "M3", "Mechanical:MountingHole", FP_MOUNT,
         {}, (mx, my, 0), (620, 260 + i * 10))

def _check():
    """Fail loudly rather than emit a board that cannot take the module."""
    for name, row in (("ESP32_LEFT", ESP32_LEFT), ("ESP32_RIGHT", ESP32_RIGHT)):
        if len(row) != ESP32_PINS_PER_SIDE:
            raise SystemExit(
                "%s has %d entries but ESP32_PINS_PER_SIDE is %d. The pin "
                "map has not been updated for this module -- fill it in "
                "from the board's silkscreen before generating."
                % (name, len(row), ESP32_PINS_PER_SIDE))
    seen = {}
    for side, row in (("L", ESP32_LEFT), ("R", ESP32_RIGHT)):
        for i, (label, net) in enumerate(row):
            if net in ("-", "GND", "+3V3", "+5V"):
                continue
            if net in seen:
                raise SystemExit("net %s is on two ESP32 pins: %s and %s%d"
                                 % (net, seen[net], side, i + 1))
            seen[net] = "%s%d" % (side, i + 1)


_check()

PARTS = P

# --------------------------------------------------------------------------
# Net classes
# --------------------------------------------------------------------------
POWER_NETS = ["GND", "+12V", "+12V_IN", "+12V_F", "+5V", "+5V_CAM", "SW_NODE",
              "GPS_VCC",
              "MOT_A1", "MOT_A2", "MOT_B1", "MOT_B2", "+3V3"]
TRACK_POWER = 1.0
TRACK_SIG = 0.4
CLEARANCE = 0.3
VIA_D = 0.8
VIA_DRILL = 0.4

# --------------------------------------------------------------------------
# Silkscreen notes placed on the board
# --------------------------------------------------------------------------
SILK = [
    (ESP_X + ESP32_ROW_SPACING / 2, 9.5, "ESP32 DevKit 38p  USB->", 1.0),
    (TMC_X + TMC_ROW_SPACING / 2, 10.5, "TMC2209", 1.2),
    (22, 5, "12V IN  2.5-3A", 1.2),
    (22, 67, "LM2596 5V 3A", 1.0),
    (63, 45, "CAM 5V SWITCHED", 1.0),
    (108, 57, "MOTOR A1 A2 B1 B2", 1.0),
]
