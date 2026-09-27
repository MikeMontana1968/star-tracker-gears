/* figs_mech.js -- the mechanical figures. Every dimension here is taken from
   star_tracker_gears.scad / baseplate_geom.py / build.ps1. Datum: plate top z = 0. */
(function () {
  'use strict';
  var FIG = window.FIG = window.FIG || {};
  var P = ISO.P;
  var SH = [[0, 0], [67.5, 0], [67.5, 67.5], [0, 67.5], [0, -52.5]];
  var LOBE = [26, 20, 20, 20, 34];
  var T = 4.3;                       // plate, as ordered
  var S4 = SH[4];
  function rad(a) { return a * Math.PI / 180; }

  /* ---------- geometry helpers ---------- */
  function hull2(pts) {
    pts = pts.slice().sort(function (a, b) { return a[0] - b[0] || a[1] - b[1]; });
    function cr(o, a, b) { return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]); }
    var lo = [], up = [], i, p;
    for (i = 0; i < pts.length; i++) { p = pts[i]; while (lo.length > 1 && cr(lo[lo.length - 2], lo[lo.length - 1], p) <= 0) lo.pop(); lo.push(p); }
    for (i = pts.length - 1; i >= 0; i--) { p = pts[i]; while (up.length > 1 && cr(up[up.length - 2], up[up.length - 1], p) <= 0) up.pop(); up.push(p); }
    lo.pop(); up.pop(); return lo.concat(up);
  }
  var HULL = (function () {
    var pts = [];
    SH.forEach(function (c, i) { for (var a = 0; a < 360; a += 2) pts.push([c[0] + LOBE[i] * Math.cos(rad(a)), c[1] + LOBE[i] * Math.sin(rad(a))]); });
    return hull2(pts);
  })();
  function plateHoles() {
    var out = [[0, 0, 23, 'motor']], r = 31 / Math.SQRT2, i, a;
    [45, 135, 225, 315].forEach(function (d) { out.push([r * Math.cos(rad(d)), r * Math.sin(rad(d)), 3.4, 'mot']); });
    SH.slice(1, 4).forEach(function (s) { out.push([s[0], s[1], 4.9, 'nail']); });
    out.push([S4[0], S4[1], 22.4, 'tower']);
    for (i = 0; i < 3; i++) { a = rad(i * 120 + 60); out.push([S4[0] + 17.5 * Math.cos(a), S4[1] + 17.5 * Math.sin(a), 3.4, 'fl']); }
    for (i = 0; i < 4; i++) { a = rad(i * 90 + 45); out.push([S4[0] + 25 * Math.cos(a), S4[1] + 25 * Math.sin(a), 5.5, 'm5']); }
    return out;
  }
  function D2(pts, close) { return 'M' + pts.map(function (p) { return ISO.n(p[0]) + ' ' + ISO.n(p[1]); }).join('L') + (close ? 'Z' : ''); }
  function bb2(pts) { var b = [1e9, 1e9, -1e9, -1e9]; pts.forEach(function (p) { b[0] = Math.min(b[0], p[0]); b[1] = Math.min(b[1], p[1]); b[2] = Math.max(b[2], p[0]); b[3] = Math.max(b[3], p[1]); }); return b; }
  // horizontal cylinder along +x (the +x end is the near one)
  function hcyl(s, x0, y, z, len, r, cls) {
    var A = [], B = [];
    for (var i = 0; i < 40; i++) {
      var t = i * Math.PI / 20;
      A.push(P(x0, y + r * Math.cos(t), z + r * Math.sin(t)));
      B.push(P(x0 + len, y + r * Math.cos(t), z + r * Math.sin(t)));
    }
    var h = hull2(A.concat(B));
    s.raw('<path class="' + (cls || 'm') + '" d="' + D2(h, true) + '"/><path class="' + (cls || 'm') + '" d="' + D2(B, true) + '"/>', bb2(h));
  }
  function flat(s, w, h, svg) { s.raw(svg, [0, 0, w, h]); return { scale: 1, pad: 0 }; }

  /* ---------- parts ---------- */
  function plate(s, o) {
    o = o || {};
    var m = o.mirror ? -1 : 1;           // mirror = the plate turned over, seen from above
    s.prism(HULL.map(function (p) { return [p[0], m * p[1]]; }), -T + (o.dz || 0), T, { smooth: true, cls: o.cls });
    if (o.holes !== false) plateHoles().forEach(function (h) {
      if (o.skip && o.skip.indexOf(h[3]) >= 0) return;
      s.hole(h[0], m * h[1], o.dz || 0, h[2] / 2, T);
    });
  }
  function motorBody(s, dz, o) {
    o = o || {};
    var a = 21, c = 4.5, zt = -T + (dz || 0), oct = [[-a + c, -a], [a - c, -a], [a, -a + c], [a, a - c], [a - c, a], [-a + c, a], [-a, a - c], [-a, -a + c]];
    s.prism(oct, zt - 20, 20, { side: 'f' });
    s.box(-7, 21, zt - 15, 14, 3.5, 7);                        // connector, on the side AWAY from S4
    if (o.top) {
      [[15.5, 15.5], [15.5, -15.5], [-15.5, 15.5], [-15.5, -15.5]].forEach(function (p) { s.hole(p[0], p[1], zt, 1.5, 3); });
      s.cyl(0, 0, zt, 11, 2);
    }
  }
  function motorShaft(s, dz, top) { s.cyl(0, 0, -T + 2 + (dz || 0), 2.5, top - (-T + 2), { cls: 'm' }); }
  function spacer(s, i, z, h) { s.cyl(SH[i][0], SH[i][1], z, 5, h, { hole: 2.6 }); }
  function stageGear(s, i, z, final) {
    var x = SH[i][0], y = SH[i][1];
    s.gear(x, y, z, 120, 1, 5, { hub: 4.75 });
    if (final) s.gear(x, y, z + 5, 24, 2, 7, { hub: 9, bore: 2.6 });
    else s.gear(x, y, z + 5, 15, 1, 7, { bore: 2.6 });
  }
  function pinion0(s, z) { s.gear(0, 0, z, 15, 1, 7, { bore: 2.7 }); }
  function nailTip(s, i, from, top) { if (top > from) s.cyl(SH[i][0], SH[i][1], from, 2.44, top - from, { cls: 'm' }); }
  var NAILTOP = [0, 15.7, 22.7, 28.7];
  // Rev B lower half: spigot tube z -21..-10.8, clamp disc 41 x 6.5 up to the
  // plate underside at -4.3
  var ZT = -T;                        // top of the lower half
  function towerLower(s, dz, o) {
    o = o || {}; dz = dz || 0;
    var x = S4[0], y = S4[1];
    s.cyl(x, y, -21 + dz, 11, 21 - 10.8);
    s.cyl(x, y, -10.8 + dz, 20.5, 6.5);
    if (o.face) {                       // the plate-facing face
      s.hole(x, y, ZT + dz, 2.9, 3);
      for (var i = 0; i < 3; i++) { var a = rad(i * 120); s.hole(x + 8 * Math.cos(a), y + 8 * Math.sin(a), ZT + dz, 1.6, 3); }
      for (i = 0; i < 3; i++) { a = rad(i * 120 + 60); s.hole(x + 17.5 * Math.cos(a), y + 17.5 * Math.sin(a), ZT + dz, 1.35, 3); }
    }
  }
  function towerUpper(s, dz, o) {
    o = o || {}; dz = dz || 0;
    var x = S4[0], y = S4[1];
    s.cyl(x, y, dz, 22, 4);
    for (var i = 0; i < 3; i++) { var a = rad(i * 120 + 60); if (!o.noFl) s.hole(x + 17.5 * Math.cos(a), y + 17.5 * Math.sin(a), 4 + dz, 1.7, 3); }
    s.cyl(x, y, 4 + dz, 11, 10);
    if (o.bearing) s.cyl(x, y, 14 + dz, 8.25, 0.01, { rings: [7.1, 4.2], hole: 2.5 });
    else s.hole(x, y, 14 + dz, 8.25, 5);
  }
  function hubLower(s, dz, o) {
    o = o || {}; dz = dz || 0;
    var x = S4[0], y = S4[1];
    s.cyl(x, y, 15 + dz, 22, 6.5);
    for (var i = 0; i < 6; i++) { var a = rad(i * 60); s.hole(x + 18 * Math.cos(a), y + 18 * Math.sin(a), 21.5 + dz, 1.35, 2); }
    s.cyl(x, y, 21.5 + dz, 11, 4.5, { hole: o.shaft ? null : 2.6 });
  }
  function wheel96(s, dz) {
    s.gear(S4[0], S4[1], 21.5 + (dz || 0), 96, 2, 5, { bolts: [6, 18], bore: 11 });
  }
  function hubUpper(s, dz, o) {
    o = o || {}; dz = dz || 0;
    var x = S4[0], y = S4[1];
    var zt = 36.5 + dz;                // Rev B: 10 mm thick
    s.cyl(x, y, 26.5 + dz, 22, 10);
    for (var i = 0; i < 6; i++) { var a = rad(i * 60); if (o.heads) { s.cyl(x + 18 * Math.cos(a), y + 18 * Math.sin(a), zt, 2.75, 3, { cls: 'm' }); s.socket(x + 18 * Math.cos(a), y + 18 * Math.sin(a), zt + 3, 1.3); } else s.hole(x + 18 * Math.cos(a), y + 18 * Math.sin(a), zt, 1.7, 3); }
    s.hole(x, y, zt, 3.3, 4);
  }
  function camera(s, z) {
    var x = S4[0], y = S4[1];
    s.box(x - 10, y - 10, z, 20, 20, 11);              // mount buckle
    s.box(x - 31, y - 16.5, z + 11, 62, 33, 45);        // HERO6, 62 x 33 x 45
    s.vcirc(x - 13, y + 16.5, z + 36, 9.5, 'y', 'f');
    s.vcirc(x - 13, y + 16.5, z + 36, 6, 'y', 'sh');
    s.cyl(x + 18, y - 4, z + 56, 4, 1.5);
  }
  function bracket(s, dz, o) {
    o = o || {}; dz = dz || 0;
    var x = S4[0], y = S4[1];
    if (o.board !== false) s.box(x - 10, y - 7.5, -41.6 + dz, 20, 15, 1.6, { cls: 'pcbf' });
    s.cyl(x, y, -40 + dz, 15, 29.2, { hole: 11.15, depth: 8 });
  }

  /* ---------- the assembled machine, in painter's order ---------- */
  // lvl: 1 S1 | 2 pinion | 3 S2 | 4 S3 | 5 tower | 6 hub+shaft | 7 wheel+upper hub | 8 camera
  function machine(s, lvl, o) {
    o = o || {};
    if (lvl >= 9) bracket(s, 0);
    motorBody(s, 0);
    plate(s, { skip: lvl >= 5 ? ['tower', 'fl'] : [] });
    motorShaft(s, 0, 9.5);
    if (lvl >= 5) { towerUpper(s, 0, { bearing: lvl < 6 }); }
    if (lvl >= 5) { for (var i = 0; i < 3; i++) { var a = rad(i * 120 + 60); s.cyl(S4[0] + 17.5 * Math.cos(a), S4[1] + 17.5 * Math.sin(a), 4, 2.75, 3, { cls: 'm' }); s.socket(S4[0] + 17.5 * Math.cos(a), S4[1] + 17.5 * Math.sin(a), 7, 1.3); } }
    // A bare S3 nail stands BEHIND the S2 wheel (S3 is 67.5 mm further from
    // the viewer), so it has to be painted first or it looks as if it
    // pierces the wheel. It does not: nail to wheel tip is 4.06 mm.
    if (lvl < 4) nailTip(s, 3, 0, NAILTOP[3]);
    if (lvl >= 1) { spacer(s, 1, 0, 2); stageGear(s, 1, 2); }
    if (lvl >= 2) pinion0(s, 2);
    nailTip(s, 1, lvl >= 1 ? 14 : 0, NAILTOP[1]);
    if (lvl >= 3) { spacer(s, 2, 0, 8.5); stageGear(s, 2, 8.5); }
    nailTip(s, 2, lvl >= 3 ? 20.5 : 0, NAILTOP[2]);
    if (lvl >= 6) hubLower(s, 0, { shaft: true });
    if (lvl >= 4) { spacer(s, 3, 0, 15); s.gear(0, 67.5, 15, 120, 1, 5, { hub: 4.75 }); }
    if (lvl >= 7) wheel96(s, 0);
    if (lvl >= 4) s.gear(0, 67.5, 20, 24, 2, 7, { hub: 9, bore: 2.6 });
    if (lvl >= 4) nailTip(s, 3, 27, NAILTOP[3]);
    if (lvl >= 7) hubUpper(s, 0, { heads: true });
    if (lvl >= 8) camera(s, 36.5);
  }

  /* ================= FIGURES ================= */
  FIG.cover = function (s) { machine(s, 9); return { w: 560 }; };

  FIG.plate_top = function (s) {
    plate(s);
    s.label([0, 0, 0], 'S0  motor', -40, -52);
    s.label([67.5, 0, 0], 'S1', -46, 20);
    s.label([67.5, 67.5, 0], 'S2', 0, 40);
    s.label([0, 67.5, 0], 'S3', 46, 22);
    s.label([0, -52.5, 0], 'S4  output', 30, -62);
    s.label([15, -35, 0], 'TOP', 0.01, 0, { bare: true, size: 15, cls: 'cnt' });
    s.rot(33.75, 33.75, 0.2, 22, 'ccw', [-60, 200]);
    return { w: 440 };
  };

  FIG.nails = function (s) {
    var L = [20, 27, 33];
    L.forEach(function (l, i) {
      var x = -i * 32, y = i * 32;
      s.nail(x, y, 0, l + 1.3);
      s.dim([x, y, 1.3], [x, y, l + 1.3], l + ' mm', i === 2 ? -30 : 26);
      s.label([x, y, 0], 'S' + (i + 1), 0, 22, { dot: false, size: 13, cls: 'cnt', bare: true });
    });
    return { w: 360 };
  };

  // the plate turned TOP-face-down, so the nails go in from what is now the top
  FIG.nail_press = function (s) {
    plate(s, { mirror: true });
    [3, 2, 1].forEach(function (i) {
      var len = [0, 20, 27, 33][i], x = SH[i][0], y = -SH[i][1], zh = 40 + len;
      s.guide([x, y, 0], [x, y, zh - len]);
      s.cyl(x, y, zh - len, 2.44, len - 1.3, { cls: 'm' });
      s.cyl(x, y, zh - 1.3, 4.45, 1.3, { cls: 'm' });
      s.arrow([x, y, zh], 'down', 30, 4);
    });
    s.count([67.5, -67.5, 60], '3x', 50, -30);
    s.label([30, -10, 0], 'TOP face DOWN', 0.01, 0, { bare: true, size: 13, cls: 'cnt' });
    return { w: 420 };
  };

  FIG.motor_trim = function (s) {
    motorBody(s, 0, { top: true });
    s.cyl(0, 0, -T + 2, 2.5, 22, { cls: 'm' });
    s.dim([0, 0, -T], [0, 0, 9.5], '13.8 mm', -38);
    s.line([-7, -7, 9.5], [7, 7, 9.5], 'cut');
    s.label([0, 0, 9.5], 'cut here', 60, -14);
    s.label([0, 0, 15], 'D-flat must reach\nthe pinion zone', -64, -26);
    return { w: 330 };
  };

  FIG.motor_mount = function (s) {
    motorBody(s, -34, { top: true });
    motorShaft(s, -34, 9.5 - 34);
    plate(s);
    [[15.5, 15.5], [15.5, -15.5], [-15.5, 15.5], [-15.5, -15.5]].forEach(function (p) {
      s.guide([p[0], p[1], -T - 34], [p[0], p[1], 12]);
    });
    [[15.5, 15.5], [15.5, -15.5], [-15.5, 15.5], [-15.5, -15.5]].forEach(function (p) { s.screw(p[0], p[1], 14, 8, { flat: true }); });
    s.arrow([0, 0, -T - 30], 'up', 38, 6);
    s.count([-15.5, 15.5, 22], '4x', 26, -14);
    s.label([0, 24.5, -T - 34 - 11], 'connector: away from S4', 60, 30);
    return { w: 440 };
  };

  FIG.s1 = function (s) {
    machine(s, 0);
    spacer(s, 1, 22, 2);
    s.guide([67.5, 0, NAILTOP[1]], [67.5, 0, 22]);
    stageGear(s, 1, 44);
    s.arrow([67.5, 0, 64], 'down', 36, 4);
    s.label([67.5, 0, 24], 'spacer 2.0', -86, 30);
    s.label([67.5 + 50, 0, 49], '120T down', -30, 50);
    return { w: 470 };
  };

  FIG.pinion = function (s) {
    machine(s, 1);
    pinion0(s, 30);
    s.guide([0, 0, 9.5], [0, 0, 30]);
    s.arrow([0, 0, 44], 'down', 32, 4);
    s.bubble([0, 0, 4], [-150, -40], 70, function (b) {
      b.raw('<rect class="f" x="0" y="0" width="70" height="50"/><rect class="f" x="70" y="0" width="50" height="70"/>' +
        '<path class="dl" d="M-14 50H134"/><path class="ar" d="M92 88l8 -12 8 12z" transform="translate(0,-4)"/>' +
        '<text class="tx" x="35" y="30" font-size="13" text-anchor="middle">120T</text><text class="tx" x="95" y="40" font-size="13" text-anchor="middle">15T</text>' +
        '<text class="tx" x="60" y="100" font-size="14" text-anchor="middle">flush</text>', [-14, -6, 134, 104]);
    });
    return { w: 470 };
  };

  FIG.s2 = function (s) {
    machine(s, 2);
    spacer(s, 2, 30, 8.5);
    s.guide([67.5, 67.5, NAILTOP[2]], [67.5, 67.5, 30]);
    stageGear(s, 2, 50);
    s.arrow([67.5, 67.5, 70], 'down', 36, 4);
    s.label([67.5, 67.5, 34], 'spacer 8.5', 80, 18);
    return { w: 470 };
  };

  FIG.s3 = function (s) {
    machine(s, 3);
    spacer(s, 3, 36, 15);
    s.guide([0, 67.5, NAILTOP[3]], [0, 67.5, 36]);
    stageGear(s, 3, 62, true);
    s.arrow([0, 67.5, 82], 'down', 36, 4);
    s.label([0, 67.5, 44], 'spacer 15.0', 84, 10);
    s.label([0, 67.5 + 18, 72], '24T coarse up', 70, -30);
    return { w: 470 };
  };

  FIG.turn = function (s) {
    machine(s, 4);
    s.rot(67.5, 0, 10, 66, 'ccw', [-40, 70]);
    return { w: 470 };
  };

  // a slice of plate around S4, so views without the whole plate show the gap
  function plateSlice(s, dz) { s.cyl(S4[0], S4[1], -T + (dz || 0), 30, T, { hole: 11.2, depth: T }); }

  // Rev B: dowels stand in the lower half's disc face, ready for the plate
  FIG.tower_dowels = function (s) {
    towerLower(s, 0, { face: true });
    for (var i = 0; i < 3; i++) {
      var a = rad(i * 120), x = S4[0] + 8 * Math.cos(a), y = S4[1] + 8 * Math.sin(a);
      s.guide([x, y, ZT], [x, y, 14]);
      s.cyl(x, y, 14, 1.45, 11.9, { cls: 'm' });
    }
    s.arrow([S4[0] + 8, S4[1], 30], 'down', 26, 2);
    s.count([S4[0] + 8, S4[1], 32], '3x', 60, -4);
    s.label([S4[0], S4[1] + 20.5, -8], 'disc face UP', 70, 16);
    return { w: 300 };
  };

  FIG.bearings = function (s) {
    // lower half: bearing goes UP into the pocket in its bottom face
    s.bearing(S4[0], S4[1], -52);
    s.arrow([S4[0], S4[1], -52], 'up', 30, 34);
    s.guide([S4[0], S4[1], -47], [S4[0], S4[1], -21]);
    towerLower(s, 0);
    // upper half, drawn above: bearing DOWN into its top face
    towerUpper(s, 26, { bearing: false });
    s.guide([S4[0], S4[1], 40], [S4[0], S4[1], 58]);
    s.bearing(S4[0], S4[1], 58);
    s.arrow([S4[0], S4[1], 63], 'down', 30, 2);
    s.label([S4[0], S4[1] - 20.5, -8], 'LOWER', -60, 0);
    s.label([S4[0], S4[1] - 22, 28], 'UPPER', -60, 0);
    return { w: 300 };
  };

  // Rev B: lower half under, plate between, upper half on top, 3 x M3x14 down
  FIG.tower_mount = function (s) {
    towerLower(s, -36, { face: true });
    for (var i = 0; i < 3; i++) { var a = rad(i * 120); s.cyl(S4[0] + 8 * Math.cos(a), S4[1] + 8 * Math.sin(a), ZT - 36, 1.45, 7.9, { cls: 'm' }); }
    s.guide([S4[0], S4[1], ZT - 36], [S4[0], S4[1], -T]);
    s.arrow([S4[0], S4[1], -52], 'up', 26, 26);
    plate(s, { skip: [] });
    towerUpper(s, 26, { bearing: true });
    s.guide([S4[0], S4[1], 0], [S4[0], S4[1], 26]);
    for (i = 0; i < 3; i++) {
      var b = rad(i * 120 + 60), x = S4[0] + 17.5 * Math.cos(b), y = S4[1] + 17.5 * Math.sin(b);
      s.guide([x, y, 30], [x, y, 58]);
      s.screw(x, y, 72, 14);
    }
    s.arrow([S4[0], S4[1], 56], 'down', 26, 2);
    s.count([S4[0] + 17.5, S4[1], 76], '3x', 60, -10);
    s.label([S4[0] + 20.5, S4[1], -40], 'lower half,\ndowels up', 70, 20);
    return { w: 440 };
  };

  FIG.shaft_cut = function (s) {
    hcyl(s, 0, 0, 0, 55, 2.5, 'm');
    hcyl(s, 55, 0, 0, 10, 2.5, 'ft');
    s.line([55, -6, 0], [55, 6, 0], 'cut');
    s.dim([0, 0, 3], [55, 0, 3], '55 mm', 14);
    s.label([60, 0, -2.5], 'waste 10', 20, 26);
    return { w: 360 };
  };

  FIG.hub_shaft = function (s) {
    towerLower(s, 0); plateSlice(s); towerUpper(s, 0, { bearing: true });
    s.cyl(S4[0], S4[1], 24, 3.6, 1, { hole: 2.6 });
    s.label([S4[0], S4[1] + 3.6, 24.5], 'printed washer', 76, 4);
    hubLower(s, 20);
    s.cyl(S4[0], S4[1], 58, 2.5, 55, { cls: 'm' });
    s.guide([S4[0], S4[1], 14], [S4[0], S4[1], 24]);
    s.arrow([S4[0], S4[1], 116], 'down', 34, 2);
    s.label([S4[0] - 22, S4[1], 38.5], 'grub: finger-tight\nfor now', -80, 6);
    return { w: 330 };
  };

  FIG.cap = function (s) {
    towerLower(s, 0); plateSlice(s); towerUpper(s, 0, { bearing: true });
    s.cyl(S4[0], S4[1], -33, 2.5, 12, { cls: 'm' });
    s.cyl(S4[0], S4[1], -62, 4.5, 9, { hole: 2.6 });
    s.cyl(S4[0], S4[1], -80, 3, 2.5, { cls: 'm' });
    s.guide([S4[0], S4[1], -77.5], [S4[0], S4[1], -62]);
    s.arrow([S4[0], S4[1], -64], 'up', 28, 22);
    s.label([S4[0], S4[1] + 3, -79], 'magnet 6 x 2.5\ndiametric', 70, 8);
    s.label([S4[0], S4[1] + 4.5, -58], 'magnet cap', 74, -4);
    return { w: 320 };
  };
  FIG.cap_gauge = function (s) {
    s.cyl(S4[0], S4[1], -21, 11, 10.2);
    s.cyl(S4[0], S4[1], -37, 4.5, 9);
    s.cyl(S4[0], S4[1], -28, 2.5, 7, { cls: 'm' });
    s.dim([S4[0] - 11, S4[1] - 11, -21], [S4[0] - 11, S4[1] - 11, -37], '16.0', -34);
    return { w: 230 };
  };

  FIG.wheel = function (s) {
    towerLower(s, 0); plateSlice(s); towerUpper(s, 0, { bearing: true });
    hubLower(s, 0, { shaft: true });
    wheel96(s, 16);
    s.guide([S4[0], S4[1], 26], [S4[0], S4[1], 37.5]);
    s.nut(S4[0], S4[1], 50, 11.1, 5.6, 3.3);
    s.arrow([S4[0], S4[1], 48], 'up', 22, 4);
    hubUpper(s, 40);
    for (var i = 0; i < 2; i++) { var a = rad(i * 60); s.guide([S4[0] + 18 * Math.cos(a), S4[1] + 18 * Math.sin(a), 76.5], [S4[0] + 18 * Math.cos(a), S4[1] + 18 * Math.sin(a), 98]); }
    for (i = 0; i < 6; i++) { a = rad(i * 60); s.screw(S4[0] + 18 * Math.cos(a), S4[1] + 18 * Math.sin(a), 118, 20); }
    s.count([S4[0] + 18, S4[1], 124], '6x', 70, -12);
    s.label([S4[0] + 5, S4[1] + 5, 53], '1/4"-20 nut', -96, 8);
    s.label([S4[0] + 70, S4[1] + 20, 42], '96T', 40, 10, { cls: 'cnt', size: 15 });
    return { w: 460 };
  };

  FIG.as5600 = function (s) {
    plate(s, { holes: false, cls: 'f' });
    towerLower(s, 0);
    bracket(s, -34);
    s.guide([S4[0], S4[1], -45], [S4[0], S4[1], -21]);
    s.arrow([S4[0], S4[1], -80], 'up', 30, 4);
    for (var i = -1; i <= 1; i += 2) {
      s.guide([S4[0] + i * 8.5, S4[1], -100], [S4[0] + i * 8.5, S4[1], -75.6]);
      s.screw(S4[0] + i * 8.5, S4[1], -106, 6, { up: true });
    }
    s.count([S4[0] + 8.5, S4[1], -112], '2x', 50, 18);
    s.label([S4[0], S4[1] + 7.5, -75], 'AS5600, chip UP', 80, 6);
    s.label([S4[0] + 20.5, S4[1], -10.8], 'push up until it stops\non the disc', 90, -8);
    return { w: 380 };
  };

  FIG.wedge = function (s) {
    s.box(-45, -97.5, -80, 90, 90, 8);
    s.hole(S4[0], S4[1], -72, 21, 8);
    bracket(s, 0);
    towerLower(s, 0);
    plate(s);
    for (var i = 0; i < 4; i++) {
      var a = rad(i * 90 + 45), x = S4[0] + 25 * Math.cos(a), y = S4[1] + 25 * Math.sin(a);
      s.guide([x, y, 0], [x, y, 20]);
      s.screw(x, y, 22, 14, { d: 5 });
    }
    s.count([S4[0] + 25, S4[1], 30], '4x M5', 70, -8);
    s.label([S4[0] + 30, S4[1] + 45, -76], 'your wedge adapter', 70, 26);
    return { w: 420 };
  };

  FIG.camera = function (s) {
    towerUpper(s, 0, { noFl: true });
    hubLower(s, 0, { shaft: true }); wheel96(s, 0); hubUpper(s, 0, { heads: true });
    s.guide([S4[0], S4[1], 36.5], [S4[0], S4[1], 60]);
    s.screw(S4[0], S4[1], 62, 10, { d: 6.35, hr: 5.5, hh: 1.2 });
    camera(s, 76);
    s.arrow([S4[0] - 31, S4[1] - 16.5, 144], 'down', 30, 4);
    s.label([S4[0], S4[1], 57], '1/4"-20, 10 mm max', 96, 10);
    return { w: 400 };
  };

  /* ---------- hardware for the bags ---------- */
  FIG.hw_m3x8 = function (s) { s.screw(0, 0, 0, 8); return { w: 70, max: 7 }; };
  FIG.hw_m3x14 = function (s) { s.screw(0, 0, 0, 14); return { w: 70, max: 7 }; };
  FIG.hw_m3x20 = function (s) { s.screw(0, 0, 0, 20); return { w: 70, max: 7 }; };
  FIG.hw_m3x8f = function (s) { s.screw(0, 0, 0, 8, { flat: true }); return { w: 70, max: 7 }; };
  FIG.hw_m3x6 = function (s) { s.screw(0, 0, 0, 6); return { w: 70, max: 7 }; };
  FIG.hw_grub = function (s) { s.grub(0, 0, 0, 4); return { w: 40, max: 7 }; };
  FIG.hw_nut = function (s) { s.nut(0, 0, 0, 5.5, 2.4, 1.5); return { w: 56, max: 7 }; };
  FIG.hw_qnut = function (s) { s.nut(0, 0, 0, 11.1, 5.6, 3.3); return { w: 80, max: 6 }; };
  FIG.hw_shim = function (s) { s.cyl(0, 0, 0, 5, 0.5, { cls: 'm', hole: 2.5 }); return { w: 70, max: 7 }; };
  FIG.hw_nail = function (s) { s.nail(0, 0, 0, 34); return { w: 50, max: 3.4 }; };
  FIG.hw_shaft = function (s) { s.cyl(0, 0, 0, 2.5, 55, { cls: 'm' }); return { w: 40, max: 2.2 }; };
  FIG.hw_625 = function (s) { s.bearing(0, 0, 0); return { w: 90, max: 6 }; };
  FIG.hw_magnet = function (s) { s.cyl(0, 0, 0, 3, 2.5, { cls: 'm' }); return { w: 60, max: 8 }; };
  FIG.hw_dowel = function (s) { s.cyl(0, 0, 0, 1.45, 7.6, { cls: 'm' }); return { w: 30, max: 6 }; };
  FIG.hw_m5 = function (s) { s.screw(0, 0, 0, 16, { d: 5 }); return { w: 60, max: 4.2 }; };

  /* ---------- printed parts ---------- */
  FIG.pp_pinion = function (s) { s.gear(0, 0, 0, 15, 1, 7, { bore: 2.7 }); return { w: 80, max: 5 }; };
  FIG.pp_stage = function (s) { s.gear(0, 0, 0, 120, 1, 5, { hub: 4.75 }); s.gear(0, 0, 5, 15, 1, 7, { bore: 2.6 }); return { w: 190 }; };
  FIG.pp_final = function (s) { s.gear(0, 0, 0, 120, 1, 5, { hub: 4.75 }); s.gear(0, 0, 5, 24, 2, 7, { hub: 9, bore: 2.6 }); return { w: 190 }; };
  FIG.pp_wheel = function (s) { s.gear(0, 0, 0, 96, 2, 5, { bolts: [6, 18], bore: 11 }); return { w: 250 }; };
  FIG.pp_tower = function (s) {
    s.cyl(0, 0, 0, 11, 10.2); s.cyl(0, 0, 10.2, 20.5, 6.5, { hole: 2.9 });
    for (var i = 0; i < 3; i++) { var a = rad(i * 120); s.hole(8 * Math.cos(a), 8 * Math.sin(a), 16.7, 1.6, 3); }
    s.cyl(70, -70, 0, 22, 4); s.cyl(70, -70, 4, 11, 10); s.hole(70, -70, 14, 8.25, 5);
    s.label([0, 11, 3], 'LOWER', -30, 30, { dot: false });
    s.label([70, -59, 3], 'UPPER', 30, 30, { dot: false });
    return { w: 230, max: 3 };
  };
  FIG.pp_hubs = function (s) {
    s.cyl(0, 0, 0, 22, 7.5); s.cyl(0, 0, 7.5, 11, 4.5, { hole: 2.6 });
    s.cyl(70, -70, 0, 22, 7.5, { hole: 3.3 });
    for (var i = 0; i < 6; i++) { var a = rad(i * 60); s.hole(70 + 18 * Math.cos(a), -70 + 18 * Math.sin(a), 7.5, 1.7, 3); s.hole(18 * Math.cos(a), 18 * Math.sin(a), 7.5, 1.35, 2); }
    s.label([0, 11, 3], 'LOWER', -30, 30, { dot: false });
    s.label([70, -59, 3], 'UPPER', 30, 30, { dot: false });
    return { w: 230, max: 3 };
  };
  FIG.pp_small = function (s) {
    s.cyl(0, 0, 0, 4.5, 9, { hole: 3 });
    s.cyl(-22, 22, 0, 3.6, 1, { hole: 2.6 });
    s.cyl(40, -40, 0, 15, 29.2, { hole: 11.15, depth: 8 });
    s.label([0, 4.5, 0], 'cap', -20, 26, { dot: false });
    s.label([-22, 25.6, 0], 'washer', -10, 26, { dot: false });
    s.label([40, -25, 0], 'bracket', 26, 26, { dot: false });
    return { w: 190, max: 4 };
  };
  FIG.pp_spacers = function (s) {
    [[2, '2.0'], [8.5, '8.5'], [15, '15.0']].forEach(function (h, i) {
      s.cyl(-i * 22, i * 22, 0, 5, h[0], { hole: 2.6 });
      s.label([-i * 22, i * 22 + 5, 0], h[1], 0, 26, { dot: false, bare: true, size: 13 });
    });
    return { w: 170, max: 4 };
  };
  FIG.pp_motor = function (s) { motorBody(s, T, { top: true }); s.cyl(0, 0, 2, 2.5, 20, { cls: 'm' }); return { w: 150, max: 3 }; };

  /* ---------- Z bands: a side elevation of the whole stack ---------- */
  FIG.zbands = function (s) {
    var W = 640, H = 330, z0 = 290, k = 7.2, cw = 118;   // px per mm vertically
    function Y(z) { return z0 - z * k; }
    var col = function (i) { return 40 + i * cw; };
    var svg = '';
    for (var z = 0; z <= 34; z += 5) svg += '<path class="grid" d="M30 ' + Y(z) + 'H' + (W - 12) + '"/><text class="tx dim" x="24" y="' + (Y(z) + 4) + '" font-size="11" text-anchor="end">' + z + '</text>';
    svg += '<rect class="f" x="30" y="' + Y(0) + '" width="' + (W - 42) + '" height="' + (T * k) + '"/>';
    svg += '<text class="tx" x="' + (W - 18) + '" y="' + (Y(0) + 20) + '" font-size="11" text-anchor="end">baseplate 4.3</text>';
    var st = [
      { i: 0, name: 'S0 motor', sp: 0, w: null, p: [2, 7, '15T'] },
      { i: 1, name: 'S1', sp: 2, w: [2, 5, '120T'], p: [7, 7, '15T'] },
      { i: 2, name: 'S2', sp: 8.5, w: [8.5, 5, '120T'], p: [13.5, 7, '15T'] },
      { i: 3, name: 'S3', sp: 15, w: [15, 5, '120T'], p: [20, 7, '24T m2'] },
      { i: 4, name: 'S4 output', sp: 0, w: [21.5, 5, '96T m2'], p: null }
    ];
    st.forEach(function (c) {
      var x = col(c.i);
      svg += '<path class="ax" d="M' + (x + 44) + ' ' + Y(-1) + 'V' + Y(35) + '"/>';
      if (c.sp) svg += '<rect class="sp" x="' + (x + 36) + '" y="' + Y(c.sp) + '" width="16" height="' + (c.sp * k) + '"/>';
      if (c.w) svg += '<rect class="f" x="' + (x - 10) + '" y="' + Y(c.w[0] + c.w[1]) + '" width="62" height="' + (c.w[1] * k) + '"/><text class="tx" x="' + (x + 21) + '" y="' + (Y(c.w[0] + c.w[1] / 2) + 4) + '" font-size="11" text-anchor="middle">' + c.w[2] + '</text>';
      if (c.p) svg += '<rect class="f" x="' + (x + 52) + '" y="' + Y(c.p[0] + c.p[1]) + '" width="' + (c.p[2].indexOf('24') === 0 ? 52 : 40) + '" height="' + (c.p[1] * k) + '"/><text class="tx" x="' + (x + 52 + (c.p[2].indexOf('24') === 0 ? 26 : 20)) + '" y="' + (Y(c.p[0] + c.p[1] / 2) + 4) + '" font-size="11" text-anchor="middle">' + c.p[2] + '</text>';
      svg += '<text class="tx cnt" x="' + (x + 44) + '" y="' + (z0 + 38) + '" font-size="13" text-anchor="middle">' + c.name + '</text>';
    });
    svg += '<path class="warnln" d="M30 ' + Y(19) + 'H' + (col(2)) + '"/><text class="tx" x="' + (col(0) + 2) + '" y="' + (Y(19) - 5) + '" font-size="11">S0, S1 nail tips stay under 19 mm, the 96T passes over them</text>';
    return flat(s, W, H + 40, svg);
  };

  window.MECH = { plate: plate, machine: machine, hcyl: hcyl, flat: flat };
})();
