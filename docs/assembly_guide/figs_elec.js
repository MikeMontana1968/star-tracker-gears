/* figs_elec.js -- flat (2D) figures: PCB orientation panels, the meter,
   wiring, section views of the known issues, and the how-to-use diagrams. */
(function () {
  'use strict';
  var FIG = window.FIG = window.FIG || {};
  var flat = function (s, w, h, svg) { s.raw(svg, [0, 0, w, h]); return { scale: 1, pad: 0 }; };
  function T(x, y, t, o) {
    o = o || {};
    return '<text class="' + (o.cls || 'tx') + '" x="' + x + '" y="' + y + '" font-size="' + (o.size || 12) + '" text-anchor="' + (o.a || 'middle') + '">' +
      String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;') + '</text>';
  }
  function L(x1, y1, x2, y2, c) { return '<path class="' + (c || 't') + '" d="M' + x1 + ' ' + y1 + 'L' + x2 + ' ' + y2 + '"/>'; }
  function lead(x, y, tx, ty, t) { return '<circle class="k" cx="' + x + '" cy="' + y + '" r="2"/>' + L(x, y, tx, ty) + T(tx + (tx < x ? -4 : 4), ty + 4, t, { a: tx < x ? 'end' : 'start' }); }
  function pad(x, y, sq, r) { r = r || 8; return sq ? '<rect class="pad" x="' + (x - r) + '" y="' + (y - r) + '" width="' + 2 * r + '" height="' + 2 * r + '"/><circle class="drill" cx="' + x + '" cy="' + y + '" r="' + r * 0.42 + '"/>' : '<circle class="pad" cx="' + x + '" cy="' + y + '" r="' + r + '"/><circle class="drill" cx="' + x + '" cy="' + y + '" r="' + r * 0.42 + '"/>'; }
  function board(x, y, w, h) { return '<rect class="pcb" x="' + x + '" y="' + y + '" width="' + w + '" height="' + h + '" rx="3"/>'; }
  function guide(x, y1, y2) { return '<path class="dl" d="M' + x + ' ' + y1 + 'V' + y2 + '"/>'; }
  function arrowUp(x, y) { return '<path class="ar" d="M' + x + ' ' + y + 'l-8 12h5v14h6v-14h5z"/>'; }
  function arrowDown(x, y) { return '<path class="ar" d="M' + x + ' ' + y + 'l-8 -12h5v-14h6v14h5z"/>'; }

  /* ---------------- the little person ---------------- */
  function man(x, y, sc, mood) {
    var mouth = mood === 'sad' ? '<path class="l" d="M37 39 Q45 33 53 39"/>' : mood === 'hmm' ? '<path class="l" d="M37 37 Q41 34 45 37 Q49 40 53 37"/>' : '<path class="l" d="M36 34 Q45 43 54 34"/>';
    return '<g transform="translate(' + x + ' ' + y + ') scale(' + sc + ')">' +
      '<path class="l" d="M22 70 C4 80 6 100 22 104"/><path class="l" d="M68 70 C86 80 84 100 68 104"/>' +
      '<path class="f" d="M28 50 C12 60 8 88 16 110 L21 150 H69 L74 110 C82 88 78 60 62 50 Z"/>' +
      '<path class="l" d="M45 124 V150"/>' +
      '<ellipse class="f" cx="45" cy="28" rx="18" ry="21"/>' +
      '<circle class="k" cx="38.5" cy="24" r="1.9"/><circle class="k" cx="51.5" cy="24" r="1.9"/>' + mouth + '</g>';
  }
  FIG.man_ok = function (s) { return flat(s, 100, 160, man(5, 4, 1, 'ok')); };
  FIG.man_hmm = function (s) {
    return flat(s, 170, 160, man(5, 4, 1, 'hmm') + '<circle class="f" cx="128" cy="40" r="26"/>' + T(128, 52, '?', { size: 34, cls: 'cnt' }) +
      '<circle class="f" cx="96" cy="70" r="5"/><circle class="f" cx="86" cy="84" r="3"/>');
  };
  FIG.man_call = function (s) {
    return flat(s, 300, 160, man(5, 4, 1, 'ok') +
      '<path class="l" d="M60 20 c14 -4 18 10 8 16"/>' +
      '<path class="l" d="M70 40 C120 70 130 120 180 118"/>' +
      '<rect class="f" x="180" y="60" width="110" height="90" rx="4"/>' + T(235, 88, 'RESUME.md', { size: 14, cls: 'cnt' }) +
      T(235, 110, 'DESIGN_NOTES.md', { size: 11 }) + T(235, 128, 'PCB.md', { size: 11 }) + T(235, 144, 'FIRMWARE.md', { size: 11 }));
  };

  /* ---------------- tools ---------------- */
  FIG.tools = function (s) {
    var g = '';
    // hex keys
    g += '<path class="k" d="M20 20 h8 v70 h-8z"/><path class="k" d="M20 82 h26 v8 h-26z"/>' + T(33, 112, '2.5 mm hex') + T(33, 126, '1.5 mm hex', { size: 11 });
    // calipers
    g += '<path class="f" d="M80 20 h110 v12 h-110z"/><path class="f" d="M84 32 h10 v34 l-10 -8z"/><path class="f" d="M140 32 h12 v34 l-12 -8z"/><rect class="f" x="138" y="14" width="28" height="24" rx="3"/>' + T(135, 112, 'calipers');
    // countersink bit
    g += '<path class="m" d="M222 20 h10 v44 l12 14 h-34 l12 -14z"/><path class="l" d="M218 78 l9 18 l9 -18"/>' + T(228, 112, 'countersink');
    // soldering iron
    g += '<path class="f" d="M270 90 l18 -18 l30 -30 l8 8 l-30 30 l-18 18z"/><path class="l" d="M326 42 l14 -14"/>' + T(305, 112, 'iron, flux');
    // flush cutters
    g += '<path class="f" d="M372 96 l10 -46 l6 0 l-4 46z"/><path class="f" d="M408 96 l-10 -46 l-6 0 l4 46z"/><path class="k" d="M382 50 l6 -26 l4 26z"/>' + T(390, 112, 'flush cutters');
    // multimeter
    g += '<rect class="f" x="440" y="20" width="46" height="72" rx="6"/><rect class="f" x="447" y="27" width="32" height="18"/>' + T(463, 41, '5.00', { size: 10, cls: 'mono' }) + '<circle class="l" cx="463" cy="68" r="11"/>' + T(463, 112, 'multimeter');
    // cut-off wheel
    g += '<circle class="f" cx="530" cy="50" r="24"/><circle class="k" cx="530" cy="50" r="4"/><path class="f" d="M530 50 l40 30 l-6 8 l-38 -32z"/>' + T(540, 112, 'cut-off wheel');
    return flat(s, 590, 130, g);
  };

  /* ---------------- check panels ---------------- */
  FIG.roll_ok = function (s) {
    return flat(s, 220, 90, '<path class="l" d="M10 70 H210"/><rect class="m" x="40" y="58" width="140" height="11" rx="2"/><rect class="m" x="34" y="54" width="7" height="19" rx="1"/>' +
      '<path class="ra" d="M70 40 H150"/><path class="k" d="M150 34 l12 6 l-12 6z"/>' + T(110, 88, 'rolls flat, no daylight'));
  };
  FIG.roll_no = function (s) {
    return flat(s, 220, 90, '<path class="l" d="M10 70 H210"/><path class="m" d="M40 66 Q110 46 180 66 L180 55 Q110 36 40 55 Z"/>' +
      '<path class="l" d="M100 70 V58 M112 70 V55 M124 70 V57"/>' + T(110, 88, 'rocks, shows a gap'));
  };
  FIG.square_ok = function (s) {
    return flat(s, 160, 110, '<rect class="f" x="10" y="80" width="140" height="14"/><rect class="m" x="72" y="20" width="10" height="60"/><rect class="m" x="66" y="94" width="22" height="5"/>' +
      '<path class="f" d="M92 80 V24 h8 v48 h40 v8z"/>');
  };
  FIG.square_no = function (s) {
    return flat(s, 160, 110, '<rect class="f" x="10" y="80" width="140" height="14"/><path class="m" d="M72 80 L88 22 L97 24 L82 80 Z"/><rect class="m" x="66" y="94" width="22" height="5"/>' +
      '<path class="f" d="M106 80 V24 h8 v48 h30 v8z"/>');
  };
  function bearingSection(x, y) {
    return '<rect class="m" x="' + (x - 40) + '" y="' + y + '" width="16" height="30"/><rect class="m" x="' + (x + 24) + '" y="' + y + '" width="16" height="30"/>' +
      '<rect class="m" x="' + (x - 21) + '" y="' + y + '" width="9" height="30"/><rect class="m" x="' + (x + 12) + '" y="' + y + '" width="9" height="30"/>' +
      '<circle class="f" cx="' + (x - 18.5) + '" cy="' + (y + 15) + '" r="0"/><circle class="k" cx="' + (x - 22.5) + '" cy="' + (y + 15) + '" r="4"/><circle class="k" cx="' + (x + 22.5) + '" cy="' + (y + 15) + '" r="4"/>';
  }
  FIG.press_ok = function (s) {
    return flat(s, 200, 150, '<path class="f" d="M40 70 H56 V140 H40Z M144 70 H160 V140 H144Z"/>' + bearingSection(100, 80) +
      '<path class="f" d="M58 20 H76 V76 H58Z M124 20 H142 V76 H124Z M58 20 H142 V32 H58Z"/>' + arrowDown(100, 14) + T(100, 148, 'push the OUTER race'));
  };
  FIG.press_no = function (s) {
    return flat(s, 200, 150, '<path class="f" d="M40 70 H56 V140 H40Z M144 70 H160 V140 H144Z"/>' + bearingSection(100, 80) +
      '<path class="f" d="M80 30 H120 V78 H80Z"/>' + arrowDown(100, 24) + T(100, 148, 'inner race: brinells balls'));
  };
  FIG.grub_no = function (s) {
    return flat(s, 200, 130, '<rect class="m" x="92" y="6" width="12" height="118"/><rect class="f" x="30" y="70" width="136" height="24"/><rect class="f" x="80" y="36" width="36" height="34"/>' +
      '<rect class="m" x="150" y="76" width="26" height="12"/><path class="k" d="M176 78 h6 v8 h-6z"/>' + T(100, 120, '') );
  };

  /* ---------------- known-issue sections ---------------- */
  // Rev B tower, half section: plate clamped between flange and disc
  FIG.tower_section = function (s) {
    var cx = 140, k = 4.2, Y = function (z) { return 64 - z * k; }, X = function (r) { return cx + r * k; };
    var R = function (r0, r1, z0, z1, c) { return '<rect class="' + (c || 'f') + '" x="' + X(r0) + '" y="' + Y(z1) + '" width="' + (r1 - r0) * k + '" height="' + (z1 - z0) * k + '"/>'; };
    var g = '';
    [1, -1].forEach(function (sg) {
      var a = function (r0, r1) { return sg > 0 ? [r0, r1] : [-r1, -r0]; };
      var p = a(11.2, 30); g += R(p[0], p[1], -4.3, 0);                 // plate
      p = a(2.9, 22); g += R(p[0], p[1], 0, 4);                          // flange
      p = a(8.25, 11); g += R(p[0], p[1], 4, 14);                        // upper tube wall
      p = a(2.9, 8.25); g += R(p[0], p[1], 4, 9);                        // pocket floor
      p = a(2.9, 20.5); g += R(p[0], p[1], -10.8, -4.3);                 // disc
      p = a(2.9, 11); g += R(p[0], p[1], -21, -10.8);                    // lower tube
      p = a(15, 11.15); g += R(Math.min(p[0], p[1]), Math.max(p[0], p[1]), -30, -10.8, 'sh'); // bracket wall
    });
    g += R(6.55, 9.45, -8.3, 4, 'm');                                    // dowel, spans the plate
    g += R(16, 19, -10, 4, 'm') + R(14.75, 20.25, 4, 7, 'm');             // M3x14 from the top
    g += '<path class="ax" d="M' + cx + ' 4V200"/>';
    g += lead(X(17.5), Y(6), 290, 18, 'M3x14, thread-forms into the disc');
    g += lead(X(26), Y(-2), 290, 60, 'plate, clamped');
    g += lead(X(8), Y(2), 290, 100, 'dowel crosses the plate');
    g += lead(X(20.5), Y(-8), 290, 140, 'clamp disc');
    g += lead(X(13), Y(-12), 290, 180, 'bracket stops here');
    return flat(s, 500, 200, g);
  };
  FIG.shaft_nut = function (s) {
    var cx = 150, k = 5, Y = function (z) { return 230 - (z - 10) * 5.5; }, X = function (r) { return cx + r * k; };
    var g = '';
    g += '<rect class="f" x="' + X(-22) + '" y="' + Y(21.5) + '" width="' + 44 * k + '" height="' + (Y(15) - Y(21.5)) + '"/>';
    g += '<rect class="f" x="' + X(11) + '" y="' + Y(26.5) + '" width="' + 16 * k + '" height="' + (Y(21.5) - Y(26.5)) + '"/><rect class="f" x="' + X(-27) + '" y="' + Y(26.5) + '" width="' + 16 * k + '" height="' + (Y(21.5) - Y(26.5)) + '"/>';
    g += '<rect class="f" x="' + X(-11) + '" y="' + Y(26) + '" width="' + 22 * k + '" height="' + (Y(21.5) - Y(26)) + '"/>';
    g += '<rect class="f" x="' + X(-22) + '" y="' + Y(36.5) + '" width="' + 44 * k + '" height="' + (Y(26.5) - Y(36.5)) + '"/>';
    g += '<rect class="m" x="' + X(-6.35) + '" y="' + Y(32.1) + '" width="' + 12.7 * k + '" height="' + (Y(26.5) - Y(32.1)) + '"/>';
    g += '<rect class="f" x="' + X(-3.3) + '" y="' + Y(36.5) + '" width="' + 6.6 * k + '" height="' + (Y(32.4) - Y(36.5)) + '"/>';
    g += '<rect class="m" x="' + X(-2.5) + '" y="' + Y(22) + '" width="' + 5 * k + '" height="' + (Y(10) - Y(22)) + '"/>';
    g += lead(X(6.35), Y(29.5), 262, 60, '1/4"-20 nut, 5.9 deep pocket');
    g += lead(X(3.3), Y(35), 262, 30, 'camera screw hole');
    g += lead(X(-2.5), Y(22), 40, 110, 'shaft top z 22');
    g += T(X(0), Y(18) + 4, 'lower hub, grub at z 17.5', { size: 11 }) + T(X(19), Y(24) + 4, 'wheel', { size: 10 });
    return flat(s, 300, 230, g);
  };
  FIG.head_clash = function (s) {
    var g = '<rect class="f" x="10" y="110" width="300" height="34"/><rect class="f" x="10" y="54" width="300" height="40"/>' + T(160, 78, 'S1 120T wheel, underside at 2.0 mm', { size: 11 });
    g += '<rect class="m" x="56" y="86" width="44" height="24"/><rect class="clash" x="56" y="86" width="44" height="8"/><rect class="m" x="70" y="110" width="16" height="34"/>';
    g += '<path class="m" d="M200 110 H248 L232 126 H216 Z"/><rect class="m" x="216" y="126" width="16" height="18"/>';
    g += T(78, 160, 'socket cap: hits', { size: 11 }) + T(224, 160, 'countersunk: flush', { size: 11 }) + T(30, 132, 'plate', { size: 10, a: 'start' });
    return flat(s, 320, 170, g);
  };
  FIG.shim_detail = function (s) {
    var cx = 160, k = 5, Y = function (z) { return 170 - (z - 6) * 7; }, X = function (r) { return cx + r * k; };
    var g = '';
    [1, -1].forEach(function (sg) {
      g += '<rect class="f" x="' + Math.min(X(sg * 8.25), X(sg * 11)) + '" y="' + Y(14) + '" width="' + 2.75 * k + '" height="' + (Y(6) - Y(14)) + '"/>';
      g += '<rect class="m" x="' + Math.min(X(sg * 8), X(sg * 6.6)) + '" y="' + Y(14) + '" width="' + 1.4 * k + '" height="' + 35 + '"/>';
      g += '<rect class="m" x="' + Math.min(X(sg * 4.2), X(sg * 2.5)) + '" y="' + Y(14) + '" width="' + 1.7 * k + '" height="' + 35 + '"/>';
      g += '<circle class="k" cx="' + X(sg * 5.4) + '" cy="' + (Y(14) + 17.5) + '" r="4"/>';
      g += '<rect class="hl" x="' + Math.min(X(sg * 2.6), X(sg * 3.6)) + '" y="' + Y(15) + '" width="' + 1.0 * k + '" height="' + (Y(14) - Y(15)) + '"/>';
    });
    g += '<rect class="f" x="' + X(-22) + '" y="' + Y(21.5) + '" width="' + 44 * k + '" height="' + (Y(15) - Y(21.5)) + '"/>';
    g += '<rect class="m" x="' + X(-2.5) + '" y="' + Y(21.5) + '" width="' + 5 * k + '" height="' + (Y(6) - Y(21.5)) + '"/>';
    g += T(cx, Y(18.5) + 2, 'lower hub  (turns)', { size: 11 });
    g += lead(X(11), Y(12), 300, 150, 'tower (still)');
    g += lead(X(-3.1), Y(14.5), 40, 150, 'washer: inner race only');
    return flat(s, 330, 180, g);
  };

  /* ---------------- PCB orientation panels ---------------- */
  // each: part drawn above its footprint, dashed guides from legs to pads
  FIG.pcb_legend = function (s) {
    var c = function (i, j) { return [15 + i * 150, 12 + j * 110]; }, g = '';
    var cell = function (i, j, draw, t1) { var p = c(i, j); g += '<g transform="translate(' + p[0] + ' ' + p[1] + ')">' + board(0, 0, 130, 70) + draw + '</g>' + T(p[0] + 65, p[1] + 90, t1, { size: 11.5 }); };
    cell(0, 0, pad(45, 35, true) + pad(85, 35, false), 'square pad = pin 1, +, or K');
    cell(1, 0, '<circle class="silk" cx="65" cy="35" r="26"/>' + pad(50, 35, true) + pad(80, 35, false) + T(22, 26, '+', { size: 16, cls: 'silkt' }), '"+" = long leg');
    cell(2, 0, '<path class="silk" d="M50 58 A24 24 0 1 1 88 50 L88 20"/>' + pad(56, 35, false) + pad(80, 35, true), 'flat edge = flat of the part');
    cell(0, 1, '<rect class="silk" x="22" y="18" width="86" height="30"/><rect class="silkk" x="22" y="14" width="86" height="6"/>' + pad(40, 56, true, 6) + pad(65, 56, false, 6) + pad(90, 56, false, 6), 'bold bar = metal tab side');
    cell(1, 1, '<circle class="silk" cx="50" cy="35" r="17"/>' + pad(50, 35, false, 7) + '<path class="silk" d="M67 35 H80"/>' + pad(88, 35, true, 7), 'circle = where the body stands');
    cell(2, 1, pad(30, 35, true, 6) + pad(50, 35, false, 6) + pad(70, 35, false, 6) + pad(90, 35, false, 6) + T(30, 20, '1', { size: 15, cls: 'silkt' }), '"1" marks pin 1 of a socket');
    return flat(s, 460, 230, g);
  };
  function standing(body, bentTo, kind) {
    // body over pad at x=150, bent top lead into pad at x=bentTo
    var g = '';
    if (kind === 'res') {
      g += '<rect class="f" x="136" y="60" width="28" height="78" rx="10"/>';
      [74, 86, 98, 110].forEach(function (y) { g += '<path class="l" d="M137 ' + y + 'H163"/>'; });
      g += '<path class="l" d="M137 126H163"/>';
    } else {
      g += '<rect class="kd" x="137" y="62" width="26" height="74" rx="3"/><rect class="stripe" x="137" y="68" width="26" height="9"/>';
    }
    g += '<path class="lead" d="M150 138 V184"/><path class="lead" d="M150 60 V40 Q150 30 ' + (bentTo + (bentTo < 150 ? 10 : -10)) + ' 30 Q' + bentTo + ' 30 ' + bentTo + ' 40 V184"/>';
    return g;
  }
  FIG.pcb_res = function (s) {
    var g = board(40, 196, 220, 50) + '<circle class="silk" cx="150" cy="221" r="19"/><path class="silk" d="M169 221 H172"/>' + pad(150, 221, false) + pad(180, 221, false);
    g += standing(0, 180, 'res') + guide(150, 186, 210) + guide(180, 186, 210);
    g += lead(150, 202, 70, 170, 'body over the circle') + T(250, 100, 'no polarity', { a: 'end', size: 12 });
    return flat(s, 300, 256, g);
  };
  FIG.pcb_diode = function (s) {
    var g = board(40, 196, 220, 50) + '<circle class="silk" cx="150" cy="221" r="19"/>' + pad(150, 221, false) + pad(110, 221, true) + T(84, 226, 'K', { size: 15, cls: 'silkt' });
    g += standing(0, 110, 'diode') + guide(150, 186, 210) + guide(110, 186, 210);
    g += lead(163, 72, 220, 60, 'stripe at the TOP') + lead(110, 120, 58, 120, 'bent lead');
    return flat(s, 300, 256, g);
  };
  FIG.pcb_cap = function (s) {
    var g = board(30, 196, 240, 60) + '<circle class="silk" cx="150" cy="226" r="0"/>' + '<circle class="silk" cx="150" cy="226" r="0"/>';
    g += '<path class="silk" d="M150 196 m-58 0"/>';
    g = board(30, 190, 240, 62) + '<circle class="silk" cx="150" cy="221" r="44" />';
    g += pad(128, 221, true) + pad(172, 221, false) + T(84, 212, '+', { size: 20, cls: 'silkt' });
    g += '<path class="f" d="M108 40 H192 V150 H108 Z"/><rect class="stripe2" x="170" y="40" width="22" height="110"/>';
    [66, 90, 114].forEach(function (y) { g += '<path class="lp" d="M176 ' + y + 'H186"/>'; });
    g += '<ellipse class="f" cx="150" cy="40" rx="42" ry="6"/>';
    g += '<path class="lead" d="M128 150 V196"/><path class="lead" d="M172 150 V184"/>' + guide(128, 196, 208) + guide(172, 186, 208);
    g += lead(128, 172, 60, 170, 'long leg = +') + lead(192, 96, 252, 96, 'stripe = minus');
    return flat(s, 300, 260, g);
  };
  FIG.pcb_led = function (s) {
    var g = board(40, 190, 220, 62) + '<path class="silk" d="M168 207 A26 26 0 1 0 168 235"/><path class="silk" d="M168 207 V235"/>';
    g += pad(135, 221, false) + pad(165, 221, true) + T(196, 226, 'K', { size: 15, cls: 'silkt' });
    g += '<path class="f" d="M122 110 V70 A28 28 0 0 1 178 70 V110 Z"/><path class="f" d="M116 110 H178 V120 H116 Z"/><path class="l" d="M178 110 V120"/>';
    g += '<path class="lead" d="M135 120 V200"/><path class="lead" d="M165 120 V184"/>' + guide(165, 186, 210);
    g += lead(165, 150, 230, 150, 'short leg, flat side') + lead(135, 160, 70, 160, 'long leg');
    return flat(s, 300, 260, g);
  };
  FIG.pcb_to92 = function (s) {
    var g = board(20, 176, 260, 70);
    g += '<path class="silk" d="M100 226 A38 38 0 0 1 176 226 Z"/>' + pad(108, 214, true, 7) + pad(138, 214, false, 7) + pad(168, 214, false, 7);
    g += '<path class="f" d="M100 124 A38 38 0 0 1 176 124 Z"/>' + T(138, 118, 'top view', { size: 10 });
    g += guide(108, 128, 204) + guide(138, 128, 204) + guide(168, 128, 204);
    g += lead(176, 124, 236, 150, 'flat face') + lead(176, 226, 236, 214, 'flat edge');
    // front view
    g += '<path class="f" d="M22 40 H78 V96 H22 Z"/>' + T(50, 62, '2N', { size: 11, cls: 'mono' }) + T(50, 76, '3904', { size: 11, cls: 'mono' });
    g += '<path class="lead" d="M34 96 V140 M50 96 V140 M66 96 V140"/>' + T(34, 154, 'E', { size: 11 }) + T(50, 154, 'B', { size: 11 }) + T(66, 154, 'C', { size: 11 }) + T(50, 30, 'flat face to you', { size: 10 });
    return flat(s, 290, 256, g);
  };
  FIG.pcb_to220 = function (s) {
    var g = board(30, 190, 240, 66);
    g += '<rect class="silk" x="96" y="200" width="108" height="24"/><rect class="silkk" x="96" y="196" width="108" height="6"/>' + pad(125, 238, true, 7) + pad(150, 238, false, 7) + pad(175, 238, false, 7);
    g += '<rect class="m" x="100" y="20" width="100" height="44"/><circle class="f" cx="150" cy="40" r="9"/><rect class="f" x="100" y="60" width="100" height="62"/>' + T(150, 94, 'IRF4905', { size: 12, cls: 'mono' });
    g += '<path class="lead" d="M125 122 V176 M150 122 V176 M175 122 V176"/>' + guide(125, 178, 228) + guide(150, 178, 228) + guide(175, 178, 228);
    g += lead(200, 34, 250, 24, 'metal tab') + lead(204, 199, 256, 176, 'bold bar');
    g += T(125, 138, '1', { size: 11 }) + T(62, 150, 'G D S', { size: 12, cls: 'mono' });
    return flat(s, 300, 262, g);
  };
  FIG.pcb_lm2596 = function (s) {
    var g = board(30, 190, 240, 72);
    g += '<rect class="silk" x="92" y="198" width="116" height="22"/><rect class="silkk" x="92" y="194" width="116" height="6"/>';
    var xs = [110, 130, 150, 170, 190];
    xs.forEach(function (x, i) { g += pad(x, i % 2 ? 250 : 234, i === 0, 6); });
    g += '<rect class="m" x="96" y="20" width="108" height="44"/><circle class="f" cx="150" cy="40" r="9"/><rect class="f" x="96" y="60" width="108" height="62"/>' + T(150, 88, 'LM2596T', { size: 12, cls: 'mono' }) + T(150, 104, '-5.0', { size: 12, cls: 'mono' });
    xs.forEach(function (x, i) { g += '<path class="lead" d="M' + x + ' 122 V' + (i % 2 ? 168 : 150) + ' L' + x + ' ' + (i % 2 ? 176 : 176) + '"/>' + guide(x, 178, i % 2 ? 244 : 228); });
    g += T(110, 138, '1', { size: 11 }) + lead(204, 199, 256, 176, 'tab side') + T(60, 238, '1 3 5', { size: 11, cls: 'mono' }) + T(60, 254, '2 4', { size: 11, cls: 'mono' });
    return flat(s, 300, 268, g);
  };
  FIG.pcb_socket = function (s) {
    var g = board(10, 150, 330, 60), i;
    g += '<rect class="f" x="30" y="160" width="290" height="20"/>';
    for (i = 0; i < 15; i++) g += '<rect class="k" x="' + (37 + i * 19) + '" y="166" width="7" height="7"/>';
    g += T(24, 176, '1', { size: 14, cls: 'silkt', a: 'end' });
    g += '<rect class="f" x="30" y="30" width="290" height="36" rx="3"/>' + T(175, 54, 'module, header pins down', { size: 11 });
    for (i = 0; i < 15; i++) g += '<path class="lead" d="M' + (40.5 + i * 19) + ' 66 V92"/>';
    g += guide(40.5, 96, 160) + guide(306.5, 96, 160) + T(175, 128, 'plug the module in, then solder the socket', { size: 11 });
    return flat(s, 350, 216, g);
  };
  FIG.pcb_fuse = function (s) {
    var g = board(20, 110, 300, 50);
    [90, 250].forEach(function (x, i) {
      var sg = i ? 1 : -1;
      g += '<path class="m" d="M' + (x - 16) + ' 100 V60 Q' + x + ' 36 ' + (x + 16) + ' 60 V100 Z"/>';
      g += '<path class="k" d="M' + (x + sg * 18) + ' 50 h' + (sg * 6) + ' v40 h' + (-sg * 6) + 'z"/>';
    });
    g += '<rect class="f" x="76" y="62" width="188" height="22" rx="10"/>' + T(170, 78, '3 A  5x20', { size: 11, cls: 'mono' });
    g += lead(274, 58, 300, 30, 'stop tab faces OUT');
    return flat(s, 340, 170, g);
  };

  /* ---------------- meter ---------------- */
  FIG.meter = function (s, el) {
    var v = el.getAttribute('data-v') || '5.00', a = el.getAttribute('data-a') || '+', b = el.getAttribute('data-b') || 'GND';
    var g = '<rect class="f" x="10" y="8" width="110" height="168" rx="10"/><rect class="disp" x="22" y="22" width="86" height="40" rx="2"/>' + T(65, 50, v, { size: 20, cls: 'mono' });
    g += '<circle class="l" cx="65" cy="108" r="24"/><path class="l" d="M65 108 L78 92"/><circle class="k" cx="40" cy="158" r="5"/><circle class="k" cx="90" cy="158" r="5"/>';
    g += '<path class="probe" d="M90 158 C140 170 170 60 230 50"/><path class="probe k2" d="M40 158 C80 200 180 160 230 138"/>';
    g += '<path class="k" d="M230 46 l26 4 l-26 4z"/><path class="k" d="M230 134 l26 4 l-26 4z"/>';
    g += T(262, 44, a, { a: 'start', size: 12 }) + T(262, 132, b, { a: 'start', size: 12 });
    return flat(s, 360, 186, g);
  };

  /* ---------------- wiring ---------------- */
  FIG.wiring = function (s) {
    var g = '';
    g += '<rect class="f" x="220" y="100" width="200" height="150" rx="4"/>' + T(320, 170, 'controller PCB', { size: 13, cls: 'cnt' }) + T(320, 188, 'modules fitted', { size: 11 });
    function conn(x, y, t, w, h) { g += '<rect class="k" x="' + x + '" y="' + y + '" width="' + (w || 14) + '" height="' + (h || 26) + '"/>' + T(x + (w || 14) / 2, y - 6, t, { size: 11 }); }
    conn(213, 128, 'J1'); conn(413, 118, 'J4'); conn(413, 196, 'J2', 14, 20); conn(250, 243, 'J7', 30, 14); conn(340, 243, 'J9', 34, 14);
    // battery + fuse
    g += '<rect class="f" x="18" y="110" width="110" height="60" rx="4"/><rect class="f" x="30" y="100" width="14" height="10"/><rect class="f" x="102" y="100" width="14" height="10"/>' + T(73, 136, '12 V LiFePO4', { size: 11 }) + T(73, 152, '6 Ah', { size: 11 });
    g += '<path class="wire" d="M109 100 V84 H160"/><rect class="f" x="160" y="76" width="30" height="16" rx="3"/>' + T(175, 70, '3 A', { size: 11 }) + '<path class="wire" d="M190 84 H200 V134 H213"/><path class="wire" d="M37 100 V70 H150 M150 70 H206 V148 H213"/>';
    g += '<path class="dlw" d="M20 30 H128 V60 H20 Z"/>' + T(74, 50, '20 W solar, optional', { size: 10.5 });
    // motor
    g += '<rect class="f" x="500" y="96" width="70" height="70" rx="6"/><circle class="l" cx="535" cy="131" r="12"/>' + T(535, 184, 'NEMA 17', { size: 11 });
    [122, 128, 134, 140].forEach(function (y, i) { g += '<path class="wire" d="M427 ' + (y) + ' H470 V' + (106 + i * 12) + ' H500"/>'; });
    // camera
    g += '<rect class="f" x="500" y="200" width="80" height="54" rx="6"/><circle class="l" cx="524" cy="227" r="13"/>' + T(540, 272, 'HERO6', { size: 11 });
    g += '<path class="wire" d="M427 206 C460 206 450 250 480 240 C500 232 470 214 500 216"/>' + T(466, 276, 'slack loop', { size: 10 });
    // AS5600
    g += '<rect class="pcbf" x="150" y="300" width="70" height="44"/><circle class="k" cx="185" cy="322" r="5"/>' + T(185, 358, 'AS5600 on bracket', { size: 11 });
    g += '<path class="wire" d="M265 257 V280 H200 V300"/>';
    // GPS
    g += '<rect class="pcbf" x="360" y="300" width="80" height="44"/><rect class="f" x="450" y="296" width="44" height="44"/>' + T(400, 358, 'GY-NEO6MV2', { size: 11 }) + T(472, 358, 'antenna: sky', { size: 11 });
    g += '<path class="wire" d="M357 257 V300"/><path class="dlw" d="M343 257 V284 H372 V300"/>' + T(300, 294, 'PPS lead', { size: 10 });
    return flat(s, 600, 370, g);
  };
  FIG.coils = function (s) {
    var g = '<rect class="f" x="20" y="30" width="110" height="110" rx="8"/><circle class="l" cx="75" cy="85" r="18"/>';
    [55, 75, 95, 115].forEach(function (y, i) { g += '<path class="wire" d="M130 ' + y + ' H210"/>' + T(222, y + 4, '?', { size: 13, cls: 'cnt' }); });
    g += '<path class="l" d="M236 55 h8 v20 h-8"/>' + T(252, 69, 'few ohms: one coil, A1 A2', { a: 'start', size: 11.5 });
    g += '<path class="l" d="M236 95 h8 v20 h-8"/>' + T(252, 109, 'few ohms: other coil, B1 B2', { a: 'start', size: 11.5 });
    g += T(252, 140, 'across coils: OL (open)', { a: 'start', size: 11.5 });
    return flat(s, 440, 160, g);
  };

  /* ---------------- appendix: the controller board, logically ----------------
     Every block and net below is from hardware/design.py. Signal names, not
     GPIO numbers: the ESP32 pin map is redrawn for the 30-pin module. */
  FIG.logic = function (s) {
    var g = '';
    function box(x, y, w, h, t, sub, cls) {
      g += '<rect class="' + (cls || 'f') + '" x="' + x + '" y="' + y + '" width="' + w + '" height="' + h + '" rx="4"/>';
      g += T(x + w / 2, y + (sub ? h / 2 - 3 : h / 2 + 4.5), t, { size: 12.5, cls: 'tx cnt' });
      if (sub) String(sub).split('\n').forEach(function (ln, i) { g += T(x + w / 2, y + h / 2 + 12 + i * 12, ln, { size: 10.5 }); });
    }
    function wire(pts, label, lx, ly, a, cls) {
      g += '<path class="' + (cls || 'wire') + '" d="M' + pts.map(function (p) { return p[0] + ' ' + p[1]; }).join('L') + '"/>';
      var e = pts[pts.length - 1], p = pts[pts.length - 2], dx = Math.sign(e[0] - p[0]), dy = Math.sign(e[1] - p[1]);
      if (a !== false) g += '<path class="k" d="M' + e[0] + ' ' + e[1] + 'l' + (-dx * 9 - dy * 4.5) + ' ' + (-dy * 9 - dx * 4.5) + 'l' + (dy * 9) + ' ' + (dx * 9) + 'z"/>';
      if (label) g += T(lx, ly, label, { size: 10.5, a: 'start', cls: 'mono' });
    }
    function rail(x1, x2, y, t) { g += '<path class="rail" d="M' + x1 + ' ' + y + 'H' + x2 + '"/>' + T(x2 + 6, y + 4, t, { size: 12, a: 'start', cls: 'tx cnt' }); }

    // ---- power in, top row
    box(20, 30, 80, 46, 'J1', '12 V in');
    box(130, 30, 80, 46, 'F1', '3 A fuse');
    box(240, 30, 160, 46, 'Q1 + D1 + R1', 'reverse polarity');
    box(240, 96, 160, 34, 'D2 TVS · C1 470 µF', '', 'ft');
    wire([[100, 53], [130, 53]]); wire([[210, 53], [240, 53]]);
    rail(400, 900, 53, '+12 V');
    g += '<path class="wire" d="M320 76V96"/>';
    // buck, divider, driver supply
    box(430, 96, 170, 52, 'U2 LM2596T-5.0', 'L1 · D3 · C3 · C5 · LED1');
    wire([[515, 53], [515, 96]]);
    box(640, 96, 150, 52, 'R14 / R15', '100k / 18k 1 %');
    wire([[715, 53], [715, 96]]);
    rail(40, 600, 176, '+5 V');
    g += '<path class="wire" d="M515 148V176"/>';

    // ---- ESP32 in the middle
    box(380, 226, 220, 196, 'ESP32 module', 'VIN from +5 V, 3V3 out\nOLED 0x3C on its I²C', 'f');
    wire([[470, 176], [470, 226]], 'VIN', 476, 212);
    wire([[715, 148], [715, 250], [600, 250]], 'VBAT_SENSE', 612, 244);

    // ---- driver, right
    box(740, 226, 150, 150, 'TMC2209', 'StepStick\nUART address 0\nJP4 JP5 to GND\nC11 100 µF at VM');
    wire([[860, 53], [860, 226]], 'VM', 866, 212);
    [['STEP', 280], ['MOT_DIR', 302], ['TMC_EN', 324], ['UART, R7 1k', 346]].forEach(function (r) { wire([[600, r[1]], [740, r[1]]], r[0], 604, r[1] - 4); });
    wire([[740, 368], [600, 368]], 'DIAG, J5 lead', 612, 364);
    box(920, 256, 60, 90, 'J4', 'motor\nA1 A2\nB1 B2');
    wire([[890, 300], [920, 300]]);

    // ---- switched loads, left
    box(40, 226, 220, 54, 'Q2 + Q3 camera switch', 'R3 · R4 · R5, off by default');
    wire([[150, 176], [150, 226]]);
    wire([[380, 253], [260, 253]], 'CAM_EN', 300, 248);
    box(40, 300, 220, 40, 'J2 USB-A · J3 · LED2', '+5V_CAM to the camera');
    wire([[150, 280], [150, 300]]);
    box(40, 356, 220, 54, 'Q4 + Q5 GPS switch', 'R16 – R19, off by default');
    wire([[30, 176], [30, 383], [40, 383]]);
    g += '<path class="wire" d="M30 176H40"/>';
    wire([[380, 383], [260, 383]], 'GPS_EN', 300, 378);
    box(40, 426, 220, 40, 'J9 GPS', 'GPS_VCC · RX · TX · PPS');
    wire([[150, 410], [150, 426]]);
    wire([[380, 408], [330, 408], [330, 446], [260, 446]], 'UART + PPS', 270, 440, true);

    // ---- 3V3 and I2C, bottom
    rail(40, 880, 492, '+3.3 V');
    g += '<path class="wire" d="M420 422V492"/>' + T(426, 476, '3V3', { size: 10.5, a: 'start', cls: 'mono' });
    wire([[815, 492], [815, 376]], 'VIO', 821, 470);
    rail(40, 700, 522, 'I²C  SDA · SCL');
    g += '<path class="wire" d="M500 422V522"/>' + T(506, 510, 'SDA SCL', { size: 10.5, a: 'start', cls: 'mono' });
    box(40, 548, 130, 58, 'J6 DS3231', 'RTC 0x68\nSQW to WAKE');
    box(190, 548, 130, 58, 'J7 AS5600', 'encoder 0x36\noff-board');
    box(340, 548, 110, 58, 'J10', 'I²C expansion');
    [105, 255, 395].forEach(function (x) { g += '<path class="wire" d="M' + x + ' 522V548"/>'; });
    box(470, 548, 150, 58, 'WAKE', 'R12 4.7k pull-up · C13\nSW1 to GND · SQW');
    wire([[560, 548], [560, 422]], 'WAKE', 566, 476);
    box(640, 548, 110, 58, 'J8 HOME', 'opto, R13 pull-up');
    wire([[695, 548], [695, 450], [580, 450], [580, 422]], 'HOME', 702, 470);
    box(770, 548, 130, 58, 'LED3 · J11', 'LED_ST via R8\nspare IO');
    wire([[600, 408], [760, 408], [760, 577], [770, 577]], 'LED_ST', 610, 402);
    return flat(s, 990, 620, g);
  };

  /* ---------------- first power-up and use ---------------- */
  FIG.direction = function (s) {
    var g = '<path class="l" d="M10 210 H260"/>' + T(135, 232, 'N', { size: 14, cls: 'cnt' });
    var cx = 140, cy = 80;
    g += '<path class="k" d="M' + cx + ' ' + (cy - 7) + 'l2 5 5 0 -4 3 2 5 -5 -3 -5 3 2 -5 -4 -3 5 0z"/>' + T(cx, cy - 14, 'Polaris', { size: 11 });
    [30, 52, 74].forEach(function (r) {
      var a0 = 200, a1 = 330, p = function (a) { return [cx + r * Math.cos(a * Math.PI / 180), cy + r * Math.sin(a * Math.PI / 180)]; };
      var q0 = p(a0), q1 = p(a1);
      // counter-clockwise on screen: from 330 back to 200 going over the top
      g += '<path class="ra" d="M' + q1[0].toFixed(1) + ' ' + q1[1].toFixed(1) + 'A' + r + ' ' + r + ' 0 0 0 ' + q0[0].toFixed(1) + ' ' + q0[1].toFixed(1) + '"/>';
      var t = [q0[0] - q1[0], 0]; g += '<path class="k" d="M' + (q0[0] - 3).toFixed(1) + ' ' + (q0[1] - 7).toFixed(1) + 'l-3 12 10 -6z"/>';
    });
    g += T(135, 196, 'facing north, the sky turns', { size: 11.5 }) + T(135, 250, 'counter-clockwise', { size: 11.5 });
    // platform seen from the camera end
    var px = 410, py = 110;
    g += '<circle class="f" cx="' + px + '" cy="' + py + '" r="60"/><circle class="l" cx="' + px + '" cy="' + py + '" r="8"/>';
    for (var i = 0; i < 6; i++) { var a = i * Math.PI / 3; g += '<circle class="k" cx="' + (px + 45 * Math.cos(a)).toFixed(1) + '" cy="' + (py + 45 * Math.sin(a)).toFixed(1) + '" r="3.5"/>'; }
    g += '<path class="ra" d="M' + (px - 58) + ' ' + (py - 50) + 'A76 76 0 0 1 ' + (px + 58) + ' ' + (py - 50) + '"/><path class="k" d="M' + (px + 52) + ' ' + (py - 58) + 'l12 12 -16 2z"/>';
    g += T(px, 196, 'camera platform, seen from', { size: 11.5 }) + T(px, 212, 'the camera end: clockwise', { size: 11.5 });
    g += T(px, 250, 'southern hemisphere: both reversed', { size: 10.5 });
    return flat(s, 520, 260, g);
  };
  FIG.agc = function (s) {
    var x0 = 30, x1 = 430, X = function (v) { return x0 + (x1 - x0) * v / 128; };
    var g = '<rect class="f" x="' + x0 + '" y="46" width="' + (x1 - x0) + '" height="22"/><rect class="hl" x="' + X(44) + '" y="46" width="' + (X(84) - X(44)) + '" height="22"/>';
    [0, 32, 64, 96, 128].forEach(function (v) { g += L(X(v), 68, X(v), 76) + T(X(v), 90, v, { size: 11, cls: 'mono' }); });
    g += '<path class="k" d="M' + X(64) + ' 44 l-7 -12 h14z"/>' + T(X(64), 26, 'aim for the middle', { size: 11.5 });
    g += T(x0, 112, '<- magnet too close (MH)', { a: 'start', size: 11 }) + T(x1, 112, 'too far (ML) ->', { a: 'end', size: 11 });
    return flat(s, 460, 122, g);
  };
  FIG.dial = function (s) {
    var cx = 150, cy = 140, r = 96, g = '<circle class="f" cx="' + cx + '" cy="' + cy + '" r="' + r + '"/>';
    function p(deg, rr) { var a = (deg - 90) * Math.PI / 180; return [cx + rr * Math.cos(a), cy + rr * Math.sin(a)]; }
    for (var d = 0; d < 360; d += 15) { var a = p(d, r), b = p(d, r - (d % 90 ? 6 : 12)); g += L(a[0].toFixed(1), a[1].toFixed(1), b[0].toFixed(1), b[1].toFixed(1)); }
    var s0 = p(90, r - 22), s1 = p(270.5, r - 22);
    g += '<path class="arc" d="M' + s0[0].toFixed(1) + ' ' + s0[1].toFixed(1) + 'A' + (r - 22) + ' ' + (r - 22) + ' 0 0 1 ' + s1[0].toFixed(1) + ' ' + s1[1].toFixed(1) + '"/>';
    g += '<path class="k" d="M' + (s1[0] - 2).toFixed(1) + ' ' + (s1[1] - 9).toFixed(1) + 'l-8 10 12 3z"/>';
    [[0, '0 / 4095'], [90, '1024'], [180, '2048'], [270, '3072']].forEach(function (t) { var q = p(t[0], r + 16); g += T(q[0].toFixed(1), (q[1] + 4).toFixed(1), t[1], { size: 11, cls: 'mono', a: t[0] === 90 ? 'start' : t[0] === 270 ? 'end' : 'middle' }); });
    g += T(cx, cy - 6, 'start near 1024', { size: 11 }) + T(cx, cy + 10, '180.5 deg sweep', { size: 11 }) + T(cx, cy + 26, 'never crosses the top', { size: 11 });
    return flat(s, 310, 270, g);
  };
  FIG.wedge_setup = function (s) {
    var g = '<path class="l" d="M10 320 H450"/>';
    g += '<path class="l" d="M150 236 L80 320 M150 236 L150 320 M150 236 L220 320"/><rect class="f" x="126" y="224" width="48" height="14" rx="2"/>';
    g += '<path class="f" d="M112 224 H200 L112 162 Z"/>';
    g += '<g transform="rotate(-35 112 224)"><rect class="f" x="112" y="196" width="80" height="28"/><rect class="f" x="140" y="166" width="24" height="30"/><rect class="f" x="136" y="144" width="32" height="22"/></g>';
    var ax0 = [150, 180], dir = [Math.cos(-35 * Math.PI / 180), Math.sin(-35 * Math.PI / 180)];
    var ax1 = [ax0[0] + dir[0] * 330, ax0[1] + dir[1] * 330];
    g += '<path class="ax" d="M' + ax0[0] + ' ' + ax0[1] + 'L' + ax1[0].toFixed(1) + ' ' + ax1[1].toFixed(1) + '"/>';
    g += '<path class="k" d="M' + ax1[0].toFixed(1) + ' ' + (ax1[1] - 8).toFixed(1) + 'l2.4 6 6.4 0 -5 4 2 6 -5.8 -3.6 -5.8 3.6 2 -6 -5 -4 6.4 0z"/>' + T(ax1[0] - 10, ax1[1] - 14, 'Polaris', { size: 11 });
    g += '<path class="dl" d="M112 224 H250"/><path class="l" d="M200 224 A88 88 0 0 0 184 174"/>' + T(214, 206, 'latitude', { size: 11.5, a: 'start' });
    g += T(40, 312, 'S', { size: 13, cls: 'cnt' }) + T(430, 312, 'N', { size: 13, cls: 'cnt' });
    return flat(s, 460, 330, g);
  };
  FIG.timeline = function (s) {
    var segs = [['IDLE', 60, 'off'], ['PRE', 36, 'off'], ['GPS', 40, 'off'], ['ORIENT', 56, 'off'], ['CAMERA', 60, 'STA'], ['TRACK', 196, 'off / AP'], ['STOP', 44, 'STA'], ['REWIND', 56, 'off'], ['IDLE', 52, 'off']];
    var x = 10, g = '';
    segs.forEach(function (sg) {
      g += '<rect class="' + (sg[0] === 'TRACK' ? 'hl' : 'f') + '" x="' + x + '" y="40" width="' + sg[1] + '" height="34"/>' + T(x + sg[1] / 2, 62, sg[0], { size: 10.5 }) + T(x + sg[1] / 2, 94, sg[2], { size: 9.5, cls: 'mono' });
      x += sg[1];
    });
    g += T(10, 118, 'radio:', { size: 10, a: 'start' });
    g += '<path class="k" d="M72 30 l-6 -10 h12z"/>' + T(72, 14, 'dusk alarm', { size: 10.5 });
    g += '<path class="k" d="M458 30 l-6 -10 h12z"/>' + T(458, 14, 'dawn', { size: 10.5 });
    return flat(s, 620, 124, g);
  };
  FIG.phone = function (s) {
    var g = '<rect class="f" x="10" y="6" width="210" height="400" rx="26"/><rect class="f" x="22" y="40" width="186" height="340" rx="4"/><rect class="k" x="90" y="16" width="50" height="8" rx="4"/>';
    g += T(115, 64, '192.168.4.1', { size: 11, cls: 'mono' });
    var rows = [['state', 'TRACK'], ['encoder', '97.3 deg'], ['AGC', '62'], ['battery', '13.1 V'], ['next stop', '05:48'], ['camera', 'recording']];
    rows.forEach(function (r, i) { var y = 96 + i * 30; g += T(34, y, r[0], { a: 'start', size: 12 }) + T(196, y, r[1], { a: 'end', size: 12, cls: 'mono' }) + L(34, y + 10, 196, y + 10, 'grid'); });
    ['Schedule', 'Jog', 'Test track'].forEach(function (b, i) { g += '<rect class="f" x="34" y="' + (282 + i * 30) + '" width="162" height="24" rx="12"/>' + T(115, 298 + i * 30, b, { size: 12 }); });
    return flat(s, 230, 412, g);
  };
})();
