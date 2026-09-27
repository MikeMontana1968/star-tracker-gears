/* engine.js -- a small isometric line-art renderer.
   World units are millimetres, z up, baseplate top face = z 0.
   Screen: x right, y down.  Proper (non-mirrored) isometric, so a rotation
   that is counter-clockwise seen from above also looks counter-clockwise. */
(function () {
  'use strict';
  var C = 0.8660254, S = 0.5, EX = C * Math.SQRT2, EY = S * Math.SQRT2;
  function n(v) { return Math.round(v * 100) / 100; }
  function P(x, y, z) { return [(y - x) * C, (x + y) * S - z]; }
  function bbOf(pts) {
    var b = [Infinity, Infinity, -Infinity, -Infinity];
    for (var i = 0; i < pts.length; i++) {
      var p = pts[i];
      if (p[0] < b[0]) b[0] = p[0]; if (p[1] < b[1]) b[1] = p[1];
      if (p[0] > b[2]) b[2] = p[0]; if (p[1] > b[3]) b[3] = p[1];
    }
    return b;
  }
  function D(pts, close) {
    return 'M' + pts.map(function (p) { return n(p[0]) + ' ' + n(p[1]); }).join('L') + (close ? 'Z' : '');
  }
  function esc(t) { return String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;'); }
  function unit(v) { var l = Math.hypot(v[0], v[1]) || 1; return [v[0] / l, v[1] / l]; }
  function wdir(dx, dy, dz) { var a = P(0, 0, 0), b = P(dx, dy, dz); return unit([b[0] - a[0], b[1] - a[1]]); }

  function Scene() { this.items = []; }
  var sp = Scene.prototype;
  sp.add = function (svg, bb) { this.items.push({ svg: svg, bb: bb }); return this; };
  sp.dyn = function (svgFn, bbFn) { this.items.push({ svg: svgFn, bb: bbFn }); return this; };
  // 2D content in screen units (for flat diagrams)
  sp.raw = function (svg, bb) { return this.add(svg, bb); };

  sp.geomBB = function () {
    var b = [Infinity, Infinity, -Infinity, -Infinity];
    this.items.forEach(function (it) {
      if (typeof it.bb === 'function' || !it.bb) return;
      b = [Math.min(b[0], it.bb[0]), Math.min(b[1], it.bb[1]), Math.max(b[2], it.bb[2]), Math.max(b[3], it.bb[3])];
    });
    if (!isFinite(b[0])) b = [0, 0, 1, 1];
    return b;
  };
  sp.fullBB = function (k) {
    var b = this.geomBB();
    this.items.forEach(function (it) {
      if (typeof it.bb !== 'function') return;
      var c = it.bb(k);
      b = [Math.min(b[0], c[0]), Math.min(b[1], c[1]), Math.max(b[2], c[2]), Math.max(b[3], c[3])];
    });
    return b;
  };
  sp.inner = function (k, ctx) {
    return this.items.map(function (it) { return typeof it.svg === 'function' ? it.svg(k, ctx) : it.svg; }).join('');
  };

  /* ---------------- world primitives ---------------- */
  sp.poly = function (pts3, cls) {
    var q = pts3.map(function (p) { return P(p[0], p[1], p[2]); });
    return this.add('<path class="' + (cls || 'f') + '" d="' + D(q, true) + '"/>', bbOf(q));
  };
  sp.line = function (a, b, cls) {
    var p = P(a[0], a[1], a[2]), q = P(b[0], b[1], b[2]);
    return this.add('<path class="' + (cls || 'l') + '" d="' + D([p, q]) + '"/>', bbOf([p, q]));
  };
  sp.polyline = function (pts3, cls) {
    var q = pts3.map(function (p) { return P(p[0], p[1], p[2]); });
    return this.add('<path class="' + (cls || 'l') + '" d="' + D(q) + '"/>', bbOf(q));
  };
  sp.axis = function (x, y, z0, z1) { return this.line([x, y, z0], [x, y, z1], 'ax'); };
  sp.guide = function (a, b) { return this.line(a, b, 'dl'); };

  sp.ell = function (x, y, z, r, cls) {
    var c = P(x, y, z), rx = r * EX, ry = r * EY;
    return this.add('<ellipse class="' + (cls || 'f') + '" cx="' + n(c[0]) + '" cy="' + n(c[1]) + '" rx="' + n(rx) + '" ry="' + n(ry) + '"/>',
      [c[0] - rx, c[1] - ry, c[0] + rx, c[1] + ry]);
  };
  // a hole in a horizontal face: outline plus the shaded far wall
  sp.hole = function (x, y, z, r, depth) {
    var c = P(x, y, z), X = c[0], Y = c[1], rx = r * EX, ry = r * EY;
    var dd = Math.min(depth == null ? 3 : depth, ry * 1.3);
    var yi = Y + dd / 2, xi = rx * Math.sqrt(Math.max(0, 1 - (dd / 2) * (dd / 2) / (ry * ry)));
    var s = '<ellipse class="f" cx="' + n(X) + '" cy="' + n(Y) + '" rx="' + n(rx) + '" ry="' + n(ry) + '"/>';
    if (xi > 0.2) {
      s += '<path class="sh" d="M' + n(X - xi) + ' ' + n(yi) + 'A' + n(rx) + ' ' + n(ry) + ' 0 0 1 ' + n(X + xi) + ' ' + n(yi) +
        'A' + n(rx) + ' ' + n(ry) + ' 0 1 0 ' + n(X - xi) + ' ' + n(yi) + 'Z"/>';
      s += '<path class="t" d="M' + n(X - xi) + ' ' + n(yi) + 'A' + n(rx) + ' ' + n(ry) + ' 0 0 1 ' + n(X + xi) + ' ' + n(yi) + '"/>';
    }
    return this.add(s, [X - rx, Y - ry, X + rx, Y + ry]);
  };
  sp.cyl = function (x, y, z, r, h, o) {
    o = o || {};
    var c = P(x, y, z), X = c[0], Yb = c[1], Yt = Yb - h, rx = r * EX, ry = r * EY, cl = o.cls || 'f';
    var s = '<path class="' + cl + '" d="M' + n(X - rx) + ' ' + n(Yt) + 'L' + n(X - rx) + ' ' + n(Yb) + 'A' + n(rx) + ' ' + n(ry) +
      ' 0 0 0 ' + n(X + rx) + ' ' + n(Yb) + 'L' + n(X + rx) + ' ' + n(Yt) + 'Z"/>';
    s += '<ellipse class="' + cl + '" cx="' + n(X) + '" cy="' + n(Yt) + '" rx="' + n(rx) + '" ry="' + n(ry) + '"/>';
    this.add(s, [X - rx, Yt - ry, X + rx, Yb + ry]);
    if (o.rings) for (var i = 0; i < o.rings.length; i++) this.ell(x, y, z + h, o.rings[i], 'lt');
    if (o.hole) this.hole(x, y, z + h, o.hole, o.depth == null ? h : o.depth);
    return this;
  };
  sp.frustum = function (x, y, z0, r0, z1, r1, o) {
    o = o || {};
    var a = P(x, y, z0), b = P(x, y, z1), rx0 = r0 * EX, ry0 = r0 * EY, rx1 = r1 * EX, ry1 = r1 * EY, cl = o.cls || 'f';
    var s = '<path class="' + cl + '" d="M' + n(b[0] - rx1) + ' ' + n(b[1]) + 'L' + n(a[0] - rx0) + ' ' + n(a[1]) + 'A' + n(rx0) + ' ' + n(ry0) +
      ' 0 0 0 ' + n(a[0] + rx0) + ' ' + n(a[1]) + 'L' + n(b[0] + rx1) + ' ' + n(b[1]) + 'Z"/>';
    s += '<ellipse class="' + cl + '" cx="' + n(b[0]) + '" cy="' + n(b[1]) + '" rx="' + n(rx1) + '" ry="' + n(ry1) + '"/>';
    this.add(s, [Math.min(a[0] - rx0, b[0] - rx1), b[1] - ry1, Math.max(a[0] + rx0, b[0] + rx1), a[1] + ry0]);
    if (o.hole) this.hole(x, y, z1, o.hole, 3);
    return this;
  };
  // extruded polygon. smooth:true draws the side as one band (for curves);
  // otherwise every visible face is drawn, which gives gear teeth their edges.
  sp.prism = function (poly, z, h, o) {
    o = o || {};
    var A = 0, i;
    for (i = 0; i < poly.length; i++) { var a = poly[i], b = poly[(i + 1) % poly.length]; A += a[0] * b[1] - b[0] * a[1]; }
    var pts = A < 0 ? poly.slice().reverse() : poly, N = pts.length, vis = [];
    for (i = 0; i < N; i++) {
      var p = pts[i], q = pts[(i + 1) % N];
      vis.push((q[1] - p[1]) - (q[0] - p[0]) > 1e-9);
    }
    var s = '';
    if (o.smooth) {
      var st = -1;
      for (i = 0; i < N; i++) if (vis[i] && !vis[(i - 1 + N) % N]) { st = i; break; }
      if (st >= 0) {
        var chain = [pts[st]], j = st;
        while (vis[j]) { j = (j + 1) % N; chain.push(pts[j]); if (j === st) break; }
        var top = chain.map(function (p) { return P(p[0], p[1], z + h); });
        var bot = chain.slice().reverse().map(function (p) { return P(p[0], p[1], z); });
        s += '<path class="' + (o.cls || 'f') + '" d="' + D(top.concat(bot), true) + '"/>';
      }
    } else {
      var quads = [];
      for (i = 0; i < N; i++) {
        if (!vis[i]) continue;
        var u = pts[i], v = pts[(i + 1) % N];
        quads.push([u[0] + u[1] + v[0] + v[1], [P(u[0], u[1], z), P(v[0], v[1], z), P(v[0], v[1], z + h), P(u[0], u[1], z + h)]]);
      }
      quads.sort(function (m, w) { return m[0] - w[0]; });
      if (quads.length) s += '<path class="' + (o.side || 'gs') + '" d="' + quads.map(function (q) { return D(q[1], true); }).join('') + '"/>';
    }
    var topF = pts.map(function (p) { return P(p[0], p[1], z + h); });
    s += '<path class="' + (o.cls || 'f') + '" d="' + D(topF, true) + '"/>';
    return this.add(s, bbOf(topF.concat(pts.map(function (p) { return P(p[0], p[1], z); }))));
  };
  function gearPts(N, m, cx, cy, ph) {
    var ra = m * (N / 2 + 1), rf = m * (N / 2 - 1.25), a = 2 * Math.PI / N, out = [];
    for (var k = 0; k < N; k++) {
      var t = (ph || 0) + k * a;
      [[-0.29, rf], [-0.13, ra], [0.13, ra], [0.29, rf]].forEach(function (d) {
        out.push([cx + d[1] * Math.cos(t + d[0] * a), cy + d[1] * Math.sin(t + d[0] * a)]);
      });
    }
    return out;
  }
  sp.gear = function (x, y, z, N, m, h, o) {
    o = o || {};
    this.prism(gearPts(N, m, x, y, o.ph), z, h, { side: 'gs', cls: o.cls || 'f' });
    var rf = m * (N / 2 - 1.25);
    if (N > 40 && o.web !== false) this.ell(x, y, z + h, rf - 3.2 * m, 'lt');
    if (o.hub) this.ell(x, y, z + h, o.hub, 'lt');
    if (o.bolts) for (var i = 0; i < o.bolts[0]; i++) {
      var t = i * 2 * Math.PI / o.bolts[0] + (o.bolts[2] || 0);
      this.hole(x + o.bolts[1] * Math.cos(t), y + o.bolts[1] * Math.sin(t), z + h, 1.6, 2);
    }
    if (o.bore) this.hole(x, y, z + h, o.bore, h);
    return this;
  };
  sp.box = function (x, y, z, dx, dy, dz, o) {
    o = o || {};
    var cl = o.cls || 'f';
    this.poly([[x + dx, y, z], [x + dx, y + dy, z], [x + dx, y + dy, z + dz], [x + dx, y, z + dz]], o.side || cl);
    this.poly([[x, y + dy, z], [x + dx, y + dy, z], [x + dx, y + dy, z + dz], [x, y + dy, z + dz]], o.side2 || o.side || cl);
    return this.poly([[x, y, z + dz], [x + dx, y, z + dz], [x + dx, y + dy, z + dz], [x, y + dy, z + dz]], cl);
  };
  sp.hexPrism = function (x, y, z, af, h, o) {
    o = o || {};
    var R = af / Math.sqrt(3), pts = [];
    for (var i = 0; i < 6; i++) { var t = Math.PI / 6 + i * Math.PI / 3 + (o.rot || 0); pts.push([x + R * Math.cos(t), y + R * Math.sin(t)]); }
    this.prism(pts, z, h, { side: o.cls || 'f', cls: o.cls || 'f' });
    if (o.hole) this.hole(x, y, z + h, o.hole, h);
    return this;
  };
  // hex socket on a horizontal face, filled ink
  sp.socket = function (x, y, z, r) {
    var pts = [];
    for (var i = 0; i < 6; i++) { var t = i * Math.PI / 3; pts.push([x + r * Math.cos(t), y + r * Math.sin(t), z]); }
    return this.poly(pts, 'k');
  };
  sp.threads = function (x, y, z0, z1, r) {
    var s = '', c0 = P(x, y, z0), rx = r * EX, ry = r * EY, pts = [];
    for (var t = z0 + 0.7; t < z1 - 0.3; t += 0.9) {
      var c = P(x, y, t);
      s += 'M' + n(c[0] - rx) + ' ' + n(c[1] + ry * 0.35) + 'L' + n(c[0] + rx) + ' ' + n(c[1] + ry * 0.35 - 0.6);
      pts.push(c);
    }
    return this.add('<path class="lt" d="' + s + '"/>', [c0[0] - rx, c0[1] - (z1 - z0), c0[0] + rx, c0[1] + ry]);
  };
  // socket-head cap screw pointing DOWN, head underside at z
  sp.screw = function (x, y, z, len, o) {
    o = o || {};
    var r = (o.d || 3) / 2, hr = o.hr || r * 1.83, hh = o.hh || r * 2;
    if (o.flat) {                       // countersunk: the head is part of the length
      this.cyl(x, y, z - len, r, len - r * 1.1, { cls: 'm' });
      this.threads(x, y, z - len, z - r * 1.1, r);
      this.frustum(x, y, z - r * 1.1, r, z, r * 2, { cls: 'm' });
      return this.socket(x, y, z, r * 0.9);
    }
    if (o.up) {                         // pointing UP, head underside at z
      this.cyl(x, y, z - hh, hr, hh, { cls: 'm' });
      this.cyl(x, y, z, r, len, { cls: 'm' });
      return this.threads(x, y, z, z + len, r);
    }
    this.cyl(x, y, z - len, r, len, { cls: 'm' });
    this.threads(x, y, z - len, z - (o.plain || 0), r);
    this.cyl(x, y, z, hr, hh, { cls: 'm' });
    return this.socket(x, y, z + hh, hr * 0.48);
  };
  sp.grub = function (x, y, z, len, d) {
    var r = (d || 3) / 2;
    this.cyl(x, y, z, r, len, { cls: 'm' });
    this.threads(x, y, z, z + len, r);
    return this.socket(x, y, z + len, r * 0.55);
  };
  sp.nut = function (x, y, z, af, h, hole) { return this.hexPrism(x, y, z, af, h, { cls: 'm', hole: hole }); };
  // 20d nail, head underside at z, pointing UP (driven up from underneath)
  sp.nail = function (x, y, z, len) {
    this.cyl(x, y, z, 4.45, 1.3, { cls: 'm' });
    return this.cyl(x, y, z + 1.3, 2.44, len - 1.3, { cls: 'm' });
  };
  sp.bearing = function (x, y, z) {
    return this.cyl(x, y, z, 8, 5, { cls: 'm', rings: [7.1, 4.2], hole: 2.5, depth: 5 });
  };
  // a circle in a vertical plane: axis 'x' => plane x = const; 'y' => plane y = const
  sp.vcirc = function (x, y, z, r, axis, cls) {
    var pts = [];
    for (var i = 0; i < 48; i++) {
      var t = i * Math.PI / 24, c = r * Math.cos(t), s = r * Math.sin(t);
      pts.push(axis === 'x' ? [x, y + c, z + s] : [x + c, y, z + s]);
    }
    return this.poly(pts, cls || 'f');
  };

  /* ---------------- screen-space annotations ---------------- */
  var DIRS = { down: [0, 1], up: [0, -1], left: [-1, 0], right: [1, 0] };
  sp.arrow = function (pt, dir, len, gap) {
    var U = typeof dir === 'string' ? DIRS[dir] : unit(dir), at = P(pt[0], pt[1], pt[2]);
    len = len || 44; gap = gap == null ? 6 : gap;
    function mk(k) {
      var w = 6 * k, hw = 14 * k, hl = 16 * k, L = len * k, g = gap * k;
      var tip = [at[0] - U[0] * g, at[1] - U[1] * g], bx = tip[0] - U[0] * hl, by = tip[1] - U[1] * hl;
      var tx = tip[0] - U[0] * L, ty = tip[1] - U[1] * L, nx = -U[1], ny = U[0];
      return [tip, [bx + nx * hw, by + ny * hw], [bx + nx * w, by + ny * w], [tx + nx * w, ty + ny * w],
        [tx - nx * w, ty - ny * w], [bx - nx * w, by - ny * w], [bx - nx * hw, by - ny * hw]];
    }
    return this.dyn(function (k) { return '<path class="ar" d="' + D(mk(k), true) + '"/>'; }, function (k) { return bbOf(mk(k)); });
  };
  // curved rotation arrow around a vertical axis. dir 'ccw' | 'cw' seen from above.
  sp.rot = function (x, y, z, r, dir, span) {
    span = span || [-25, 115];
    var pts = [], a0 = span[0], a1 = span[1];
    for (var i = 0; i <= 36; i++) {
      var t = (a0 + (a1 - a0) * i / 36) * Math.PI / 180;
      pts.push(P(x + r * Math.cos(t), y + r * Math.sin(t), z));
    }
    if (dir === 'cw') pts.reverse();
    var e = pts[pts.length - 1], pr = pts[pts.length - 4];
    function head(k) {
      var u = unit([e[0] - pr[0], e[1] - pr[1]]), L = 13 * k, W = 7 * k;
      return [[e[0] + u[0] * L * 0.6, e[1] + u[1] * L * 0.6], [e[0] - u[0] * L * 0.4 - u[1] * W, e[1] - u[1] * L * 0.4 + u[0] * W],
        [e[0] - u[0] * L * 0.4 + u[1] * W, e[1] - u[1] * L * 0.4 - u[0] * W]];
    }
    var body = D(pts);
    return this.dyn(function (k) { return '<path class="ra" d="' + body + '"/><path class="k" d="' + D(head(k), true) + '"/>'; },
      function (k) { return bbOf(pts.concat(head(k))); });
  };
  function textBlock(lines, x, y, fs, k, anchor, cls) {
    var s = '<text class="' + (cls || 'tx') + '" x="' + n(x) + '" y="' + n(y) + '" font-size="' + n(fs * k) + '" text-anchor="' + anchor + '">';
    lines.forEach(function (ln, i) { s += '<tspan x="' + n(x) + '" dy="' + (i ? n(fs * 1.18 * k) : 0) + '">' + esc(ln) + '</tspan>'; });
    return s + '</text>';
  }
  sp.label = function (pt, text, dx, dy, o) {
    o = o || {};
    var at = P(pt[0], pt[1], pt[2]), fs = o.size || 12.5, lines = String(text).split('\n');
    var maxc = Math.max.apply(null, lines.map(function (l) { return l.length; }));
    function geo(k) {
      var ex = at[0] + dx * k, ey = at[1] + dy * k, side = Math.abs(dx) >= Math.abs(dy);
      var anc = o.anchor || (side ? (dx < 0 ? 'end' : 'start') : 'middle');
      var tx = ex + (anc === 'start' ? 4 : anc === 'end' ? -4 : 0) * k, H = fs * 1.18 * k * lines.length;
      var ty = side ? ey - H / 2 + fs * 0.85 * k : dy < 0 ? ey - 4 * k - H + fs * 0.9 * k : ey + 4 * k + fs * 0.85 * k;
      var w = maxc * fs * 0.56 * k, x0 = anc === 'start' ? tx : anc === 'end' ? tx - w : tx - w / 2;
      return { ex: ex, ey: ey, tx: tx, ty: ty, anc: anc, bb: [Math.min(x0, at[0]), Math.min(ty - fs * k, at[1]), Math.max(x0 + w, at[0]), Math.max(ty + H - fs * 0.8 * k, at[1])] };
    }
    return this.dyn(function (k) {
      var g = geo(k), s = '';
      if (!o.bare) {
        s += '<path class="t" d="M' + n(at[0]) + ' ' + n(at[1]) + 'L' + n(g.ex) + ' ' + n(g.ey) + '"/>';
        if (o.dot !== false) s += '<circle class="k" cx="' + n(at[0]) + '" cy="' + n(at[1]) + '" r="' + n(1.9 * k) + '"/>';
      }
      return s + textBlock(lines, g.tx, g.ty, fs, k, g.anc, o.cls);
    }, function (k) { return geo(k).bb; });
  };
  // callouts in two columns outside the drawing, leaders running in to each
  // anchor. Labels never sit on the artwork; each column is spread so no two
  // labels overlap.
  sp.callouts = function (list, o) {
    o = o || {};
    var scene = this, fs = o.size || 12.5;
    var anchors = list.map(function (c) { var a = P(c.pt[0], c.pt[1], c.pt[2]); return { a: a, t: c.text, side: c.side }; });
    function layout(k) {
      var g = scene.geomBB(), cx = (g[0] + g[2]) / 2, gap = fs * 1.55 * k, out = [];
      ['L', 'R'].forEach(function (sd) {
        var col = anchors.filter(function (c) { return (c.side || (c.a[0] < cx ? 'L' : 'R')) === sd; })
          .sort(function (p, q) { return p.a[1] - q.a[1]; });
        var ys = col.map(function (c) { return c.a[1]; });
        for (var i = 1; i < ys.length; i++) ys[i] = Math.max(ys[i], ys[i - 1] + gap);
        var over = ys.length ? ys[ys.length - 1] - g[3] : 0;     // pull a long column back up
        if (over > 0) for (i = ys.length - 1; i >= 0; i--) ys[i] = Math.min(ys[i] - over, i < ys.length - 1 ? ys[i + 1] - gap : Infinity);
        var x = sd === 'L' ? g[0] - 26 * k : g[2] + 26 * k;
        col.forEach(function (c, i) {
          var w = c.t.length * fs * 0.56 * k;
          out.push({ c: c, x: x, y: ys[i], sd: sd, bb: sd === 'L' ? [x - 4 * k - w, ys[i] - fs * k, x, ys[i] + fs * 0.5 * k] : [x, ys[i] - fs * k, x + 4 * k + w, ys[i] + fs * 0.5 * k] });
        });
      });
      return out;
    }
    return this.dyn(function (k) {
      return layout(k).map(function (L) {
        var ex = L.sd === 'L' ? L.x + 10 * k : L.x - 10 * k;
        return '<path class="t" d="M' + n(L.c.a[0]) + ' ' + n(L.c.a[1]) + 'L' + n(ex) + ' ' + n(L.y) + 'L' + n(L.x) + ' ' + n(L.y) + '"/>' +
          '<circle class="k" cx="' + n(L.c.a[0]) + '" cy="' + n(L.c.a[1]) + '" r="' + n(2 * k) + '"/>' +
          textBlock([L.c.t], L.x + (L.sd === 'L' ? -4 : 4) * k, L.y + fs * 0.35 * k, fs, k, L.sd === 'L' ? 'end' : 'start', 'tx');
      }).join('');
    }, function (k) {
      var b = [Infinity, Infinity, -Infinity, -Infinity];
      layout(k).forEach(function (L) { b = [Math.min(b[0], L.bb[0]), Math.min(b[1], L.bb[1]), Math.max(b[2], L.bb[2]), Math.max(b[3], L.bb[3])]; });
      return b;
    });
  };
  sp.text = function (pt, text, o) { o = o || {}; o.bare = true; o.anchor = o.anchor || 'middle'; return this.label(pt, text, o.dx || 0.01, o.dy || 0, o); };
  sp.count = function (pt, text, dx, dy) { return this.label(pt, text, dx || 0, dy || 0, { bare: true, size: 21, cls: 'cnt', anchor: 'middle' }); };
  // dimension between two world points, offset (px) perpendicular on screen
  sp.dim = function (a, b, text, off) {
    var p = P(a[0], a[1], a[2]), q = P(b[0], b[1], b[2]), u = unit([q[0] - p[0], q[1] - p[1]]), nn = [-u[1], u[0]];
    off = off || 16;
    function geo(k) {
      var o = off * k, p2 = [p[0] + nn[0] * o, p[1] + nn[1] * o], q2 = [q[0] + nn[0] * o, q[1] + nn[1] * o];
      var m = [(p2[0] + q2[0]) / 2 + nn[0] * 11 * k * Math.sign(off), (p2[1] + q2[1]) / 2 + nn[1] * 11 * k * Math.sign(off)];
      return { p2: p2, q2: q2, m: m };
    }
    // text sits beside the dimension line: anchored away from it when the line is steep
    var side = nn[0] * Math.sign(off), anc = side > 0.3 ? 'start' : side < -0.3 ? 'end' : 'middle';
    function tw(k) { return text.length * 7.2 * k; }
    return this.dyn(function (k) {
      var g = geo(k), t = 4.5 * k, tx = g.m[0] + (anc === 'start' ? -6 : anc === 'end' ? 6 : 0) * k;
      var tick = function (c) { return 'M' + n(c[0] - (u[0] + nn[0]) * t) + ' ' + n(c[1] - (u[1] + nn[1]) * t) + 'L' + n(c[0] + (u[0] + nn[0]) * t) + ' ' + n(c[1] + (u[1] + nn[1]) * t); };
      return '<path class="t" d="' + D([p, g.p2]) + D([q, g.q2]) + D([g.p2, g.q2]) + tick(g.p2) + tick(g.q2) + '"/>' +
        textBlock([text], tx, g.m[1] + 4.5 * k, 12, k, anc, 'tx dim');
    }, function (k) {
      var g = geo(k), w = tw(k), x0 = anc === 'start' ? g.m[0] - 6 * k : anc === 'end' ? g.m[0] + 6 * k - w : g.m[0] - w / 2;
      return bbOf([p, q, g.p2, g.q2, [x0, g.m[1] - 10 * k], [x0 + w, g.m[1] + 8 * k]]);
    });
  };
  // magnified detail bubble. build(sub) draws into its own scene.
  sp.bubble = function (anc, off, R, build) {
    var sub = new Scene(); build(sub);
    var gb = sub.geomBB(), at = P(anc[0], anc[1], anc[2]);
    function geo(k) { return { c: [at[0] + off[0] * k, at[1] + off[1] * k], r: R * k }; }
    return this.dyn(function (k, ctx) {
      var g = geo(k), c = g.c, r = g.r, gw = gb[2] - gb[0], gh = gb[3] - gb[1];
      var sc = (2 * r * 0.74) / Math.max(gw, gh, 1e-6), gx = (gb[0] + gb[2]) / 2, gy = (gb[1] + gb[3]) / 2;
      var id = 'bc' + (ctx.uid++), ang = Math.atan2(at[1] - c[1], at[0] - c[0]), w = 0.2;
      var t1 = [c[0] + Math.cos(ang - w) * r, c[1] + Math.sin(ang - w) * r], t2 = [c[0] + Math.cos(ang + w) * r, c[1] + Math.sin(ang + w) * r];
      var s = '<path class="f" d="' + D([t1, at, t2]) + '"/>';
      s += '<clipPath id="' + id + '"><circle cx="' + n(c[0]) + '" cy="' + n(c[1]) + '" r="' + n(r) + '"/></clipPath>';
      s += '<circle class="f" cx="' + n(c[0]) + '" cy="' + n(c[1]) + '" r="' + n(r) + '"/>';
      s += '<g clip-path="url(#' + id + ')"><g transform="translate(' + n(c[0]) + ' ' + n(c[1]) + ') scale(' + n(sc * 1000) / 1000 + ') translate(' + n(-gx) + ' ' + n(-gy) + ')">' + sub.inner(k / sc, ctx) + '</g></g>';
      return s + '<circle class="l" cx="' + n(c[0]) + '" cy="' + n(c[1]) + '" r="' + n(r) + '"/>';
    }, function (k) { var g = geo(k); return [Math.min(g.c[0] - g.r, at[0]), Math.min(g.c[1] - g.r, at[1]), Math.max(g.c[0] + g.r, at[0]), Math.max(g.c[1] + g.r, at[1])]; });
  };

  /* ---------------- rendering ---------------- */
  var ctx = { uid: 0 };
  function render(el, FIG) {
    var name = el.getAttribute('data-fig'), fn = FIG[name];
    if (!fn) { el.textContent = '[missing figure: ' + name + ']'; return; }
    var s = new Scene(), opts = fn(s, el) || {}, g = s.geomBB();
    var W = +(el.getAttribute('data-w') || opts.w || 440), gw = Math.max(g[2] - g[0], 1e-3);
    var scale = opts.scale || +(el.getAttribute('data-scale') || 0) || Math.min(+(el.getAttribute('data-max') || opts.max || 7), W / gw);
    var k = 1 / scale, b = s.fullBB(k), pad = (opts.pad == null ? 8 : opts.pad) * k;
    var vb = [b[0] - pad, b[1] - pad, b[2] - b[0] + 2 * pad, b[3] - b[1] + 2 * pad];
    el.innerHTML = '<svg viewBox="' + vb.map(n).join(' ') + '" width="' + n(vb[2] * scale) + '" height="' + n(vb[3] * scale) +
      '" role="img" aria-label="' + esc(el.getAttribute('data-alt') || name.replace(/_/g, ' ')) + '">' + s.inner(k, ctx) + '</svg>';
  }
  window.ISO = { Scene: Scene, P: P, wdir: wdir, render: render, gearPts: gearPts, n: n };
})();
