#!/usr/bin/env python3
"""
Builds the single-file assembly guide, stjarnspar.html.

    python build.py            # regenerate the Fritzing sketch, export, build
    python build.py --no-fritzing   # reuse the last exports in _build/

What goes in:
  guide.html, engine.js, figs_mech.js, figs_elec.js   the page and its drawings
  ../../hardware/fritzing/gen_fritzing.py              wiring sketch (.fzz)
    -> Fritzing.exe -svg                               breadboard + schematic views
  ../../hardware/star_tracker_ctrl/*.kicad_sch
    -> kicad-cli sch export svg                        the full board schematic

Each export is embedded once as an SVG <symbol>. The harness close-ups in the
guide are <use> references to the breadboard symbol with a cropped viewBox,
so the page carries one copy of the Fritzing drawing, not six.
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
BUILD = os.path.join(HERE, '_build')
OUT = os.path.join(HERE, 'stjarnspar.html')
FRITZING = r'C:\Program Files\Fritzing\Fritzing.exe'
KICAD_CLI = r'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'
FZ_DIR = os.path.join(REPO, 'hardware', 'fritzing')
SCH = os.path.join(REPO, 'hardware', 'star_tracker_ctrl', 'star_tracker_ctrl.kicad_sch')

MIL_PER_MM = 1000 / 25.4       # Fritzing exports in thousandths of an inch

# Harness close-ups, as rectangles in board millimetres (board top-left = 0,0;
# the placements are in gen_fritzing.py).
CROPS = {
    'power':   (-212, -26, 245, 118),
    'motor':   (106, 20, 132, 56),
    'encoder': (-32, 78, 80, 78),
    'gps':     (52, 78, 60, 92),
    'camera':  (46, -98, 124, 118),
}


def run(cmd, **kw):
    print('>', ' '.join('"%s"' % c if ' ' in c else c for c in cmd))
    subprocess.run(cmd, check=True, **kw)


def fritzing_exports():
    os.makedirs(BUILD, exist_ok=True)
    run([sys.executable, os.path.join(FZ_DIR, 'gen_fritzing.py')])
    fx = os.path.join(BUILD, 'fritzing')
    shutil.rmtree(fx, ignore_errors=True)
    os.makedirs(fx)
    shutil.copy(os.path.join(FZ_DIR, 'star_tracker_wiring.fzz'), fx)
    run([FRITZING, '-svg', fx], timeout=600)
    run([KICAD_CLI, 'sch', 'export', 'svg', '--output', BUILD, '--exclude-drawing-sheet',
         '--no-background-color', SCH])


def read(p):
    with open(p, encoding='utf8') as fh:
        return fh.read()


def svg_parts(text):
    """(viewBox, inner markup) of an exported SVG, with ids stripped"""
    m = re.search(r'<svg\b[^>]*>', text)
    vb = re.search(r'viewBox="([^"]+)"', m.group(0)).group(1)
    inner = text[m.end():text.rindex('</svg>')]
    inner = re.sub(r'<\?xml[^>]*\?>|<!--.*?-->|<desc>.*?</desc>|<title>.*?</title>', '', inner, flags=re.S)
    inner = re.sub(r'\sid="[^"]*"', '', inner)
    return vb, inner


def kicad_clean(text):
    """KiCad draws every character as strokes and hides a real <text> behind
    them. Drop the strokes, show the text in its group's colour: ~1/4 the size
    and searchable."""
    text = re.sub(r'<g class="stroked-text">.*?</g>', '', text, flags=re.S)
    out, colour, pos = [], '#000000', 0
    for m in re.finditer(r'stroke:(#[0-9A-Fa-f]{6})|<text\b([^>]*)>', text):
        out.append(text[pos:m.start()])
        if m.group(1):
            colour = m.group(1)
            out.append(m.group(0))
        else:
            attrs = m.group(2).replace('opacity="0"', '').replace('stroke-opacity="0"', '')
            out.append('<text%s fill="%s" stroke="none" font-family="Noto Sans, Arial, sans-serif">' % (attrs, colour))
        pos = m.end()
    out.append(text[pos:])
    text = ''.join(out)
    # collapse the thousands of two-point paths KiCad emits into fewer, shorter ones
    text = re.sub(r'(\d+\.\d{2})\d+', r'\1', text)
    text = re.sub(r'\n', ' ', text)
    return text


def board_json():
    """Every footprint's reference, library name, value and absolute pad
    positions, for the isometric board drawings. Straight from the layout."""
    import json
    sys.path.insert(0, os.path.join(REPO, 'hardware'))
    sys.path.insert(0, FZ_DIR)
    import kicadlib as K
    import gen_fritzing as G
    t = K.parse(read(G.PCB))
    vals = {}
    for fp in K.kids(t, 'footprint'):
        props = {p[1]: p[2] for p in K.kids(fp, 'property')}
        vals[props.get('Reference')] = props.get('Value', '')
    fps, pads, _, _, _, _ = G.load_board()
    out = []
    for ref, (x, y, a, name) in fps.items():
        out.append(dict(r=ref, f=name.split(':')[-1], v=vals.get(ref, ''), a=a,
                        p=[[round(q['x'], 2), round(q['y'], 2), q['num']] for q in pads.get(ref, [])]))
    return json.dumps(out, separators=(',', ':'))


def main():
    if '--no-fritzing' not in sys.argv:
        fritzing_exports()
    bb = read(os.path.join(BUILD, 'fritzing', 'star_tracker_wiring_breadboard.svg'))
    sc = read(os.path.join(BUILD, 'fritzing', 'star_tracker_wiring_schematic.svg'))
    ki = kicad_clean(read(os.path.join(BUILD, 'star_tracker_ctrl.svg')))

    # where does board (0,0) land in the breadboard export? It is the first
    # instance in the sketch, so its group carries the first translate().
    t = re.search(r'translate\(([-\d.]+),\s*([-\d.]+)\)', bb)
    ox, oy = float(t.group(1)), float(t.group(2))

    bb_vb, bb_in = svg_parts(bb)
    sc_vb, sc_in = svg_parts(sc)
    ki_vb, ki_in = svg_parts(ki)
    # the schematic is label-connected (pin stub + net name, no drawn wires), so
    # draw each net's connections under the symbols or the page shows islands
    sys.path.insert(0, os.path.join(os.path.expanduser('~'), '.claude', 'skills', 'flatpack-guide', 'scripts'))
    import kicad_nets
    ki_in = kicad_nets.overlay(SCH) + ki_in
    symbols = ('<svg width="0" height="0" style="position:absolute" aria-hidden="true">'
               '<symbol id="fz-bb" viewBox="%s">%s</symbol>'
               '<symbol id="fz-sch" viewBox="%s">%s</symbol>'
               '<symbol id="ki-sch" viewBox="%s">%s</symbol></svg>' % (bb_vb, bb_in, sc_vb, sc_in, ki_vb, ki_in))

    def use(sym, vb_full, crop=None, cls='fzimg', alt=''):
        W, H = vb_full.split()[2:]
        vb = vb_full if crop is None else crop
        return ('<svg class="%s" viewBox="%s" role="img" aria-label="%s"><use href="#%s" x="0" y="0" width="%s" height="%s"/></svg>'
                % (cls, vb, alt, sym, W, H))

    def crop_vb(name):
        x, y, w, h = CROPS[name]
        return '%.1f %.1f %.1f %.1f' % (ox + x * MIL_PER_MM, oy + y * MIL_PER_MM, w * MIL_PER_MM, h * MIL_PER_MM)

    html = '<meta charset="utf-8">\n' + read(os.path.join(HERE, 'guide.html'))
    for f in ('engine.js', 'figs_mech.js', 'figs_elec.js', 'figs_pcb.js'):
        html = html.replace('<script src="%s"></script>' % f, '<script>\n%s\n</script>' % read(os.path.join(HERE, f)))
    html = html.replace('<!--ASSETS-->', symbols + '\n<script>window.BOARD=%s;</script>' % board_json())

    def fill(m):
        kind, alt = m.group(1), m.group(2)
        if kind == 'bb':
            return use('fz-bb', bb_vb, None, 'fzimg', alt)
        if kind == 'sch':
            return use('fz-sch', sc_vb, None, 'fzimg sch', alt)
        if kind == 'kicad':
            # open fitted to the drawing, not the whole A1 sheet around it
            xs, ys = [], []
            for mm in re.finditer(r'[ML]\s*([-\d.]+)[ ,]([-\d.]+)', ki_in):
                xs.append(float(mm.group(1))); ys.append(float(mm.group(2)))
            pad = 8
            fit = '%.1f %.1f %.1f %.1f' % (min(xs) - pad, min(ys) - pad, max(xs) - min(xs) + 2 * pad, max(ys) - min(ys) + 2 * pad)
            return use('ki-sch', ki_vb, fit, 'kiimg', alt)
        return use('fz-bb', bb_vb, crop_vb(kind), 'fzimg crop', alt)
    html, n = re.subn(r'<!--FZ:(\w+)\|([^>]*?)-->', fill, html)

    with open(OUT, 'w', encoding='utf8', newline='\n') as fh:
        fh.write(html)
    print('wrote %s  (%d KB, %d Fritzing/KiCad figures)' % (OUT, os.path.getsize(OUT) // 1024, n))
    if '--no-pdf' not in sys.argv:
        pdf()


EDGE = [r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
        r'C:\Program Files\Google\Chrome\Application\chrome.exe']


def pdf():
    """Print the page with headless Edge (or Chrome). The page's @media print
    rules give one sheet per Letter page on a white ground. The virtual time
    budget lets the web fonts and the figure scripts finish first."""
    exe = next((e for e in EDGE if os.path.exists(e)), None)
    if not exe:
        print('no Edge or Chrome found; skipping the PDF')
        return
    out = os.path.splitext(OUT)[0] + '.pdf'
    profile = os.path.join(BUILD, 'pdf-profile')      # throwaway, so a running Edge is not disturbed
    run([exe, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--user-data-dir=' + profile,
         '--virtual-time-budget=20000', '--print-to-pdf=' + out, 'file:///' + OUT.replace('\\', '/')], timeout=300)
    print('wrote %s  (%d KB)' % (out, os.path.getsize(out) // 1024))


if __name__ == '__main__':
    main()
