"""Floorplan: fixed positions for the big/edge parts, regions for the rest.

Board is landscape 150 x 100 mm.  The ESP32 module lies HORIZONTALLY across
the middle (its two header rows run along X, 27.94 mm apart in Y). Rev C: the
30-pin ideaspark ESP32 + OLED, 15 pins per side.

ORIENTATION -- this cost a whole board revision, so it is written down here:

The module viewed from the top with its USB at the bottom has the left column
(EN .. VIN) down the left and the right column (IO23 .. 3V3) down the right.
Laid on its side there are exactly TWO legal placements, and picking anything
else mirrors the module:

    rotate 90 CW : left column = TOP row, pin 1 at the RIGHT, USB at the LEFT
    rotate 90 CCW: left column = BOTTOM row, pin 1 at the LEFT, USB at the RIGHT

Anything else is a reflection and cannot be built.  Revision A had the left
column on top with pin 1 at the LEFT, which is neither -- every pin was wrong.
This board uses the first form: both sockets at rotation 270, origin at pin 1,
which is the RIGHT-hand end.
"""

ESP_PINS_PER_SIDE = 15            # must match design.ESP32_PINS_PER_SIDE
ESP_X = 52.0                      # left end of the pin rows (last pin)
ESP_PIN1_X = ESP_X + (ESP_PINS_PER_SIDE - 1) * 2.54   # pin 1 at the RIGHT
ESP_Y = 30.0                      # upper row = the module's LEFT column
ESP_ROT = 270                     # pads run in -X from pin 1
TMC_X = 114.0
TMC_Y = 10.0
TMC_ROT = 0                       # rows run along +Y

# ref -> (x, y, rotation)
FIXED = {
    "H1": (6, 6, 0), "H2": (144, 6, 0),
    "H3": (6, 94, 0), "H4": (144, 94, 0),

    "J1":  (14, 8, 0),          # 12 V screw terminal, top edge
    "J11": (26, 7, 90),         # spare IO, top edge
    "J2":  (60, 8, 0),          # USB-A, camera
    "J20": (ESP_PIN1_X, ESP_Y, ESP_ROT),
    "J21": (ESP_PIN1_X, ESP_Y + 27.94, ESP_ROT),
    "J30": (TMC_X, TMC_Y, TMC_ROT),
    "J31": (TMC_X + 15.24, TMC_Y, TMC_ROT),
    "J4":  (118, 48, 0),        # motor screw terminal

    # bottom connector strip, headers laid on their side
    "J7":  (18, 93, 90),
    "J6":  (34, 93, 90),
    "J8":  (57, 93, 90),
    "J10": (69, 93, 90),
    "J9":  (81, 93, 90),        # GPS, beside its power switch
    "J3":  (100, 92, 0),        # camera 5 V screw terminal
    "SW1": (122, 88, 0),        # config / wake button
}

# name -> (x0, y0, x1, y1, [refs])
REGIONS = [
    ("LEFT", 2, 14, 38, 78,
     [("F1", 90), "Q1", "D1", "R1", "D2", "C2", "C1",
      "U2", "C3", "D3", "L1", "C5", "C4", "C6", "C9"]),
    # camera switch, indicators and the battery divider. At rotation 270
    # GPIO34 is over on the right of the upper row, which is where this sits.
    ("CAM", 70, 2, 100, 26,
     ["Q2", "Q3", "R3", "R4", "R5", "C7", "C8", "R6", "LED2",
      "R8", "LED3", "R14", "R15", "C16"]),
    # x 40..100, y 58..90 is deliberately EMPTY: it is the fan-out channel
    # from the ESP32's lower row down to the bottom connector strip. Every
    # time parts were placed in it, several nets became unroutable.
    ("POCKET_L", 2, 79, 38, 90,
     ["R10", "R11", "R12", "C13", "C15", "C10", "R2", "LED1", "R13", "R16"]),
    # GPS switch beside J9, so GPS_EN and GPS_VCC stay short
    ("GPSSW", 88, 68, 104, 86,
     ["Q4", "Q5", "R17", "R18", "R19", "C17"]),
    ("TMCJ", 112, 33, 146, 42,
     ["JP4", "JP5", "JP3", "J5"]),
    ("TMCX", 112, 56, 148, 80,
     ["C11", "C12", "R7", "R9"]),
]

GAP = 1.4

# Body outlines on F.SilkS, so a module's extent is visible before it is
# fitted and you can see at a glance which way round it goes.
MODULE_OUTLINES = [
    (43.0, 27.6, 91.6, 60.4),           # ESP32 30-pin + OLED; pins 52..87.6, USB end at the left. Approximate: measure yours
    (110.2, 7.0, 131.6, 28.8),          # TMC2209 StepStick, ~20 x 15 mm
]

# silkscreen notes: (x, y, text, size)
SILK = [
    (40.5, 33, "USB", 2.2),
    (40.5, 37, "<<<", 2.2),
    (53, 34, "ESP32 30-pin + OLED -- USB END TO THE LEFT", 1.5),
    (53, 38, "PIN 1 (EN / IO23) AT THE RIGHT-HAND END", 1.4),
    (53, 42, "ROWS 27.94 mm APART -- CHECK BEFORE FITTING", 1.4),
    (53, 46, "Pin names are printed beside every pin", 1.3),
    (53, 50, "TMC2209 current set over UART", 1.3),
    (112, 4.5, "TMC2209", 1.6),
    (114, 45, "MOTOR", 1.4),
    (96, 88.5, "CAM 5V", 1.3),
    (90, 68, "GPS PWR", 1.2),
]
