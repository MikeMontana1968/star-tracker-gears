#!/usr/bin/env python3
"""1:1 print of the Ray5 marking SVG on US Letter, with a 100 mm scale bar.
Run make_baseplate_ray5_svg.py first.   python make_baseplate_ray5_pdf.py"""
import fitz
import baseplate_geom as G
import make_baseplate_ray5_svg as R

SVG = "out/docs/baseplate_ray5.svg"
OUT = "out/docs/baseplate_ray5_1to1.pdf"
MM = 72 / 25.4

svg = fitz.open(SVG)
src = fitz.open("pdf", svg.convert_to_pdf())
w, h = src[0].rect.width, src[0].rect.height          # points, from width/height in mm
doc = fitz.open()
page = doc.new_page(width=612, height=792)
x = (612 - w) / 2
y = 40
# 1 cm grid centred on the crosshair (plate bbox centre), drawn under the artwork
bx0, by0, bx1, by1 = G.bbox()
ccx, ccy = (bx0 + bx1) / 2, (by0 + by1) / 2
hh = R.NEMA17 / 2
vb_x = min(bx0, -hh) - R.MARGIN
vb_top = max(by1, hh) + R.MARGIN                       # plate-frame y at the top edge
gx = x + (ccx - vb_x) * MM                             # crosshair on the page
gy = y + (vb_top - ccy) * MM
GREY = (0.7, 0.7, 0.7)
n = -20
while n <= 20:
    px, py = gx + n * 10 * MM, gy + n * 10 * MM
    wd = 0.5 if n == 0 else 0.25
    if x <= px <= x + w:
        page.draw_line((px, y), (px, y + h), color=GREY, width=wd)
    if y <= py <= y + h:
        page.draw_line((x, py), (x + w, py), color=GREY, width=wd)
    n += 1
page.show_pdf_page(fitz.Rect(x, y, x + w, y + h), src, 0)    # no scaling
by = y + h + 18
page.draw_line((x, by), (x + 100 * MM, by), width=0.8)
for i in range(11):
    t = 4 if i % 5 else 7
    page.draw_line((x + i * 10 * MM, by), (x + i * 10 * MM, by + t), width=0.6)
page.insert_text((x, by + 18), "100 mm - measure this with a ruler. If it is not 100 mm, print at 'Actual size' / 100%.", fontsize=8)
page.insert_text((x, by + 30), "Grey grid: 1 cm, centred on the magenta crosshair.", fontsize=8)
page.insert_text((x, by + 42), "Baseplate marking template, 1:1. Image %.1f x %.1f mm." % (w / MM, h / MM), fontsize=8)
sq = 25.4 * MM
sx = x + w - sq
page.draw_rect(fitz.Rect(sx, by - 6, sx + sq, by - 6 + sq), width=0.8)
page.insert_text((sx, by - 6 + sq + 10), '1" x 1" (25.4 mm) square', fontsize=8)
doc.save(OUT)
print("wrote", OUT, "image mm: %.2f x %.2f" % (w / MM, h / MM))
