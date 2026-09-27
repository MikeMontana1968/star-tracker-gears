"""Floorplan: fixed positions for the big/edge parts, regions for the rest.

Board is landscape 127 x 76.2 mm.  The ESP32 module lies HORIZONTALLY across
the middle (its two header rows run along X, 25.4 mm apart in Y) -- a 38-pin
devkit is ~52 x 25 mm, which fits a landscape board far better end-on.

    +---------------------------------------------------------------+
    | J1 12V |      USB + CAMERA SWITCH        |   TMC2209 + MOTOR   |
    | PWR IN |---------------------------------|                     |
    | PROTECT|                                 |   motor terminal    |
    |--------|      ESP32 DevKit 38p           |---------------------|
    | BUCK   |      (horizontal)               |   MS/DIAG jumpers   |
    | 5V 3A  |---------------------------------|   VMOT + sense      |
    |        |      misc passives              |                     |
    +--------+---------------------------------+---------------------+
    |  J11    J6      J7    J8   J9    J10      CAM 5V   SW1         |
    +---------------------------------------------------------------+
"""

ESP_X = 44.0          # first pin of both ESP32 rows
ESP_Y = 24.0          # left/odd row
ESP_ROT = 90          # rows run along +X
TMC_X = 101.0
TMC_Y = 6.0
TMC_ROT = 0           # rows run along +Y

# ref -> (x, y, rotation)
FIXED = {
    "H1": (4, 4, 0), "H2": (123, 4, 0),
    "H3": (4, 80, 0), "H4": (123, 80, 0),

    "J1":  (12, 7, 0),          # 12 V screw terminal, top edge
    "J2":  (52, 7, 0),          # USB-A, opening to the left
    "J20": (ESP_X, ESP_Y, ESP_ROT),
    "J21": (ESP_X, ESP_Y + 25.4, ESP_ROT),
    "J30": (TMC_X, TMC_Y, TMC_ROT),
    "J31": (TMC_X + 15.24, TMC_Y, TMC_ROT),
    "J4":  (103, 45, 0),         # motor screw terminal

    # bottom connector strip, headers laid on their side
    "J11": (23, 6, 90),        # spare IO, top edge beside the 12V input
    "J6":  (34, 78, 90),
    "J7":  (18, 78, 90),
    "J8":  (67, 78, 90),
    "J9":  (52, 78, 90),
    "J10": (78, 78, 90),
    "J3":  (98, 77, 0),         # camera 5 V screw terminal
    "SW1": (110, 72, 0),        # config / wake button
}

# name -> (x0, y0, x1, y1, [refs in packing order])
REGIONS = [
    # tall left column: input protection above, buck below.
    # It stops at x=35 on purpose -- x 35..42 is a routing corridor around
    # the left end of the ESP32, and the two header rows cannot be crossed.
    ("LEFT", 2, 12, 35, 63,
     [("F1", 90), "Q1", "D1", "R1", "D2", "C2", "C1",
      "U2", "C3", "D3", "L1", "C5", "C4", "C6", "C9"]),
    # battery divider sits right under the ESP32 pin it feeds (GPIO34)
    ("TOPMID", 43, 15, 62, 21,
     ["R14", "R15", "C16"]),
    ("CAM", 62, 2, 93, 21,
     ["Q2", "Q3", "R3", "R4", "R5", "C7", "C8"]),
    # Pull-ups and decoupling live in a thin band directly above the
    # connectors they serve.  y 52..69 is deliberately EMPTY: that is the
    # fan-out channel from the ESP32's lower row to the bottom strip, and
    # putting passives in it is what made five nets unroutable.
    # Small passives go in a pocket below the left column, NOT in the middle.
    # x 36..92, y 52..76 is deliberately empty: it is the fan-out channel from
    # the ESP32's lower row down to the bottom connector strip, and every time
    # parts were placed in it, five nets became unroutable.
    # Small passives go in pockets at the SIDES, never in the middle.
    # x 36..92, y 52..76 is deliberately empty: it is the fan-out channel from
    # the ESP32's lower row down to the bottom connector strip, and every time
    # parts were placed in it, several nets became unroutable.
    ("POCKET_L", 2, 65, 36, 77,
     ["R10", "R11", "R12", "C13", "C15", "C10", "R2", "LED1"]),
    ("POCKET_R", 84, 60, 94, 76,
     ["R13", "R8", "LED3"]),
    ("TMCJ", 99, 30, 125, 39,
     ["JP4", "JP5", "JP3", "J5"]),
    ("TMCX", 99, 50, 126, 66,
     ["C11", "C12", "R7", "R9", "R6", "LED2"]),
]

GAP = 1.4

# silkscreen notes: (x, y, text, size)
SILK = [
    # build notes sit in the clear rectangle under the ESP32 module, so they
    # are readable exactly when you need them -- before the module goes in
    (45, 31, "VERIFY MODULE PINOUT + ROW SPACING", 1.6),
    (45, 35, "ESP32 DevKit 38-pin, USB end to the LEFT", 1.3),
    (45, 39, "Row spacing 25.4mm - see FIRMWARE.md S4", 1.3),
    (45, 43, "Pin-1 legends printed beside both rows", 1.3),
    (45, 47, "TMC2209 current is set over UART (Vref pot unused)", 1.3),
    (103, 2.5, "TMC2209", 1.5),
    (99, 40, "MOTOR", 1.3),
    (99, 68, "CAM 5V", 1.3),
]
