/* figs_beginner.js -- figures for a first-time solderer's board section.
   Generic: everything board-specific comes from window.BOARD / BOARD_INFO
   (kicad_board.py) or from data- attributes on the <figure>.

   pcb_map       top-down board, chosen parts highlighted, each labelled with
                 its value and a tick box. data-match="^R\d+$" (regex on refs)
                 and/or data-refs="C1 C3"; data-skip="R10 R11" (drawn crossed
                 out, "leave empty"); data-label="R14=100 kΩ 1%;R15=18 kΩ 1%"
                 overrides; data-k (px per mm, default fits 600 px).
   res_bands     colour bands for each value: data-values="10k 1k 100k:1%"
                 (":1%" = 5-band only). 4-band (5 %) and 5-band (1 %) rows.
   cap_code      a ceramic capacitor's three-digit code decoded: data-code="104".
   male_female   male header vs female socket, and which goes into which.
   header_snap   cutting a header strip to length.
   solder_how    the iron, the joint, the solder: heat both, feed the joint.
   joint_ok / joint_blob / joint_cold / joint_bridge    ✓/✗ joint panels.
   bend_lead     leads bent out under the board so the part stays put. */
(function () {
  'use strict';
  var FIG = window.FIG = window.FIG || {};
  var flat = function (s, w, h, svg) { s.raw(svg, [0, 0, w, h]); return { scale: 1, pad: 0 }; };
  function esc(t) { return String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;'); }
  function T(x, y, t, o) {
    o = o || {};
    return '<text class="' + (o.cls || 'tx') + '" x="' + x + '" y="' + y + '" font-size="' + (o.size || 12) + '" text-anchor="' + (o.a || 'middle') + '"' +
      (o.style ? ' style="' + o.style + '"' : '') + (o.tf ? ' transform="' + o.tf + '"' : '') + '>' + esc(t) + '</text>';
  }
  function L(x1, y1, x2, y2, c) { return '<path class="' + (c || 't') + '" d="M' + f(x1) + ' ' + f(y1) + 'L' + f(x2) + ' ' + f(y2) + '"/>'; }
  function f(n) { return Math.round(n * 10) / 10; }
  function lead(x1, y1, x2, y2) { return '<path class="lead" d="M' + x1 + ' ' + y1 + 'L' + x2 + ' ' + y2 + '"/>'; }

  /* ---------------- values, made readable ---------------- */
  // "10k" -> "10 kΩ", "330R" -> "330 Ω", "100k 1%" -> "100 kΩ 1%",
  // "470uF/25V LowESR" -> "470 µF 25 V low-ESR", "100nF" -> "100 nF"
  function pretty(v) {
    v = String(v || '').trim();
    var m = v.match(/^(\d+(?:\.\d+)?)([kKM]?)R?(\d*)(\s*\d+(?:\.\d+)?%)?$/);
    if (m && !/[uµnp]F/.test(v)) return m[1] + (m[3] ? '.' + m[3] : '') + ' ' + (m[2] ? m[2].replace('K', 'k') : '') + 'Ω' + (m[4] ? ' ' + m[4].trim() : '');
    return v.replace(/(\d)(u|µ)F/g, '$1 µF').replace(/(\d)([np])F/g, '$1 $2F').replace(/\/(\d+)V/g, ' $1 V').replace(/LowESR/i, 'low-ESR');
  }
  FIG.prettyValue = pretty;

  /* ---------------- the board, top down, parts highlighted ---------------- */
  function bboxOf(fp) {
    var xs = [], ys = [];
    fp.p.forEach(function (q) { var w = (q[4] || 1.6) / 2, h = (q[5] || 1.6) / 2; xs.push(q[0] - w, q[0] + w); ys.push(q[1] - h, q[1] + h); });
    (fp.s || []).forEach(function (it) {
      if (it.c) { xs.push(it.c[0] - it.c[2], it.c[0] + it.c[2]); ys.push(it.c[1] - it.c[2], it.c[1] + it.c[2]); }
      else for (var i = 0; i < it.length; i += 2) { xs.push(it[i]); ys.push(it[i + 1]); }
    });
    return [Math.min.apply(null, xs), Math.min.apply(null, ys), Math.max.apply(null, xs), Math.max.apply(null, ys)];
  }
  FIG.pcb_map = function (s, el) {
    var B = window.BOARD || [], I = window.BOARD_INFO || {};
    if (!B.length) return flat(s, 300, 30, T(150, 20, '[board data missing: add kicad_pcb to the build]'));
    var A = function (n) { return (el.getAttribute('data-' + n) || '').trim(); };
    var re = A('match') ? new RegExp(A('match')) : null;
    var refs = A('refs') ? A('refs').split(/[\s,]+/) : [], skip = A('skip') ? A('skip').split(/[\s,]+/) : [];
    var over = {};
    A('label').split(';').forEach(function (kv) { var i = kv.indexOf('='); if (i > 0) over[kv.slice(0, i).trim()] = kv.slice(i + 1).trim(); });
    var pick = function (fp) { return refs.indexOf(fp.r) >= 0 || (re && re.test(fp.r)); };

    var valOf = function (fp) { return skip.indexOf(fp.r) >= 0 ? 'leave empty' : (over[fp.r] || pretty(fp.v)); };
    var estW = function (ref, val) { return ref.length * 8.2 + 8 + val.length * 6.9; };
    var E = I.edge || [0, 0, 150, 100], k = +A('k') || 600 / (E[2] - E[0]), midX = (E[0] + E[2]) / 2;
    // each label column is as wide as its own longest label
    var colW = 60, colR = 60;
    B.forEach(function (fp) {
      if (!(pick(fp) || skip.indexOf(fp.r) >= 0) || !fp.p.length) return;
      var w = 15 + estW(fp.r, valOf(fp)) + 6, b = bboxOf(fp);
      if ((b[0] + b[2]) / 2 < midX) colW = Math.max(colW, w); else colR = Math.max(colR, w);
    });
    var gap = 30, top = 14, BX = colW + gap;
    var BW = (E[2] - E[0]) * k, BH = (E[3] - E[1]) * k;
    var X = function (x) { return BX + (x - E[0]) * k; }, Y = function (y) { return top + (y - E[1]) * k; };
    var g = '<rect class="pcb" x="' + f(X(E[0])) + '" y="' + f(Y(E[1])) + '" width="' + f(BW) + '" height="' + f(BH) + '" rx="' + f(1.5 * k) + '"/>';

    function silk(fp, cls, faint) {
      var o = '', st = faint ? ' style="stroke:var(--ink2)"' : '';
      (fp.s || []).forEach(function (it) {
        if (it.c) { o += '<circle class="' + cls + '"' + st + ' cx="' + f(X(it.c[0])) + '" cy="' + f(Y(it.c[1])) + '" r="' + f(it.c[2] * k) + '"/>'; return; }
        var d = '';
        for (var i = 0; i < it.length; i += 2) d += (i ? 'L' : 'M') + f(X(it[i])) + ' ' + f(Y(it[i + 1]));
        o += '<path class="' + cls + '"' + st + ' d="' + d + '"/>';
      });
      return o;
    }
    // faint = grey, not transparent: printing gives every transparent element its own group
    // the rest of the board, faint
    B.forEach(function (fp) {
      if (/^MountingHole/.test(fp.f)) { var q = fp.p[0]; if (q) g += '<circle class="lt" cx="' + f(X(q[0])) + '" cy="' + f(Y(q[1])) + '" r="' + f(1.6 * k) + '"/>'; return; }
      if (pick(fp) || skip.indexOf(fp.r) >= 0) return;
      g += silk(fp, 'lt', true);
      fp.p.forEach(function (q) { g += '<circle class="gs" cx="' + f(X(q[0])) + '" cy="' + f(Y(q[1])) + '" r="' + f(0.55 * k) + '"/>'; });
    });
    (I.text || []).forEach(function (t) {
      if (t.size < 1.4) return;
      var a = t.just === 'left' ? 'start' : t.just === 'right' ? 'end' : 'middle';
      g += T(f(X(t.x)), f(Y(t.y) + t.size * k * 0.35), t.s, { size: f(t.size * k * 0.85), a: a, style: 'fill:var(--ink2);font-weight:500', tf: t.a ? 'rotate(' + (-t.a) + ' ' + f(X(t.x)) + ' ' + f(Y(t.y)) + ')' : '' });
    });

    // the chosen parts, strong, with their labels collected
    var labels = [];
    B.forEach(function (fp) {
      var on = pick(fp), off = skip.indexOf(fp.r) >= 0;
      if (!on && !off) return;
      var b = bboxOf(fp), m = 0.7;
      g += '<rect class="' + (off ? 'gs' : 'hl') + '" x="' + f(X(b[0] - m)) + '" y="' + f(Y(b[1] - m)) + '" width="' + f((b[2] - b[0] + 2 * m) * k) + '" height="' + f((b[3] - b[1] + 2 * m) * k) + '" rx="' + f(0.8 * k) + '"/>';
      g += silk(fp, off ? 'lt' : 'l');
      fp.p.forEach(function (q) {
        var w = (q[4] || 1.6) * k, h = (q[5] || 1.6) * k, x = X(q[0]), y = Y(q[1]);
        g += q[3] === 's' ? '<rect class="pad" x="' + f(x - w / 2) + '" y="' + f(y - h / 2) + '" width="' + f(w) + '" height="' + f(h) + '"/>'
                          : '<ellipse class="pad" cx="' + f(x) + '" cy="' + f(y) + '" rx="' + f(w / 2) + '" ry="' + f(h / 2) + '"/>';
        g += '<circle class="drill" cx="' + f(x) + '" cy="' + f(y) + '" r="' + f(Math.min(w, h) * 0.22) + '"/>';
      });
      if (off) g += L(X(b[0]), Y(b[1]), X(b[2]), Y(b[3]), 'l') + L(X(b[0]), Y(b[3]), X(b[2]), Y(b[1]), 'l');
      labels.push({ ref: fp.r, val: valOf(fp), off: off, b: b,
                    cx: (b[0] + b[2]) / 2, cy: (b[1] + b[3]) / 2 });
    });

    /* Labels in two columns, leaders drawn square: a horizontal run at the
       label's height, then (if the label is not level with its part) a
       vertical drop onto the part's top or bottom edge. So leaders never run
       along each other and any crossing is at 90 degrees. Each label takes
       the free height nearest its part whose run passes no other highlighted
       part and whose drop overlaps no other drop. */
    var G = 19, H = BH + 2 * top, W = BX + BW + gap + colR + 4;
    labels.forEach(function (l) {
      var m = 0.7;
      l.P = [X(l.b[0] - m), Y(l.b[1] - m), X(l.b[2] + m), Y(l.b[3] + m)];
      l.pcx = (l.P[0] + l.P[2]) / 2; l.pcy = (l.P[1] + l.P[3]) / 2;
      l.side = l.cx < midX ? 'L' : 'R';
    });
    function route(l, t) {
      var P = l.P, L_ = l.side === 'L';
      if (t >= P[1] + 2 && t <= P[3] - 2) return { x0: L_ ? 0 : W, x1: L_ ? P[0] : P[2], v: null };
      return { x0: L_ ? 0 : W, x1: l.pcx, v: [l.pcx, t, t < P[1] ? P[1] : P[3]] };
    }
    function clear(l, r, t, taken) {
      var hx0 = Math.min(r.x0, r.x1), hx1 = Math.max(r.x0, r.x1), ok = true;
      labels.forEach(function (o) {
        if (o === l || !ok) return;
        var P = o.P;
        if (t >= P[1] - 5 && t <= P[3] + 5 && hx1 >= P[0] - 3 && hx0 <= P[2] + 3) ok = false;         // run through a part
        if (r.v) {
          var vy0 = Math.min(r.v[1], r.v[2]), vy1 = Math.max(r.v[1], r.v[2]);
          if (r.v[0] >= P[0] - 4 && r.v[0] <= P[2] + 4 && vy1 >= P[1] - 3 && vy0 <= P[3] + 3) ok = false;  // drop through a part
        }
      });
      taken.forEach(function (q) {
        if (!ok) return;
        if (q.side === l.side && Math.abs(q.t - t) < G) ok = false;                                         // labels overlap
        if (r.v && q.r.v && Math.abs(q.r.v[0] - r.v[0]) < 4 &&
            Math.max(r.v[1], r.v[2]) >= Math.min(q.r.v[1], q.r.v[2]) - 2 &&
            Math.min(r.v[1], r.v[2]) <= Math.max(q.r.v[1], q.r.v[2]) + 2) ok = false;                     // drops run together
        var qx0 = Math.min(q.r.x0, q.r.x1), qx1 = Math.max(q.r.x0, q.r.x1);
        if (r.v && q.t > Math.min(r.v[1], r.v[2]) - 2 && q.t < Math.max(r.v[1], r.v[2]) + 2 &&
            Math.abs(r.v[0] - q.r.x1) < 4 && r.v[0] >= qx0 - 4 && r.v[0] <= qx1 + 4) ok = false;              // drop ends on another run's end
      });
      return ok;
    }
    var taken = [], tMin = top - 6, tMax = top + BH + 30;
    // the parts nearest the label columns first: they get straight leaders, the far ones route round them
    labels.slice().sort(function (a, b) {
      var da = a.side === 'L' ? a.P[0] : W - a.P[2], db = b.side === 'L' ? b.P[0] : W - b.P[2];
      return da - db;
    }).forEach(function (l) {
      var best = null;
      for (var t = tMin; t <= tMax; t += 1.5) {
        var r = route(l, t), cost = Math.abs(t - l.pcy) + (r.v ? 8 : 0);
        if (best && cost >= best.cost) continue;
        if (clear(l, r, t, taken)) best = { t: t, r: r, cost: cost };
      }
      if (!best) { var t0 = l.pcy; best = { t: t0, r: route(l, t0) }; }          // nothing clear: plain straight leader
      taken.push({ l: l, side: l.side, t: best.t, r: best.r });
    });
    taken.forEach(function (q) {
      var l = q.l, t = q.t, r = q.r; H = Math.max(H, t + 14);
      var w = estW(l.ref, l.val);
      var bx = l.side === 'L' ? 4 : BX + BW + gap, tx = bx + 15;
      var sx = l.side === 'L' ? Math.min(tx + w + 4, colW + gap - 6) : bx - 4;
      g += '<rect class="' + (l.off ? 'gs' : 'f') + '" x="' + bx + '" y="' + f(t - 5) + '" width="10" height="10" rx="1"/>';
      g += '<text class="tx" x="' + tx + '" y="' + f(t + 4) + '" font-size="12.5" text-anchor="start"' + (l.off ? ' style="fill:var(--ink2)"' : '') + '><tspan font-weight="800">' +
           esc(l.ref) + '</tspan>  ' + esc(l.val) + '</text>';
      var ex = r.v ? r.v[0] : r.x1, ey = r.v ? r.v[2] : t;
      g += '<path class="t" d="M' + f(sx) + ' ' + f(t) + 'H' + f(r.x1) + (r.v ? 'V' + f(r.v[2]) : '') + '"/><circle class="k" cx="' + f(ex) + '" cy="' + f(ey) + '" r="1.8"/>';
    });
    return flat(s, W, H, g);
  };

  /* ---------------- where the meter probes go ---------------- */
  /* pcb_probe: the board top-down with each probe point marked. data-probes is
     a ;-list of role:REF:pad[:label], role red | black | bridge | pin (bridge
     ends come in pairs: a resistor or wire held between two pins; pin marks a
     hole to find, e.g. where a module's pin 1 goes, with no probe). A point that
     sits within 3.3 mm of another pad (a socket hole, a header pin, a leg in a
     row) gets a magnified bubble beside the board, with the board's own pin
     names; a stand-alone point (a screw terminal) gets a plain label. */
  var PROBE = { red: '#d42a22', black: 'var(--ink)', bridge: '#e08a00', pin: '#2563c9' };
  var uid = 0;
  function boardMM(minText, rg, m) {
    // the board drawn at page scale (x -> m.ox + x * m.k): outline, faint silk,
    // pads, text. rg = [x0, y0, x1, y1] board mm keeps only what falls inside,
    // so a bubble carries a few mm of board, not all of it. (Drawn at page
    // scale rather than in a scale() group: printing expands non-scaling
    // strokes inside a scaled group into outlines, and the PDF balloons.)
    var B = window.BOARD || [], I = window.BOARD_INFO || {}, E = I.edge || [0, 0, 150, 100], o = '', k = m.k;
    var X = function (x) { return f(m.ox + x * k); }, Y = function (y) { return f(m.oy + y * k); };
    var inR = function (x, y, mg) { mg = mg || 0; return !rg || (x >= rg[0] - mg && x <= rg[2] + mg && y >= rg[1] - mg && y <= rg[3] + mg); };
    var ex0 = rg ? Math.max(E[0], rg[0] - 1) : E[0], ey0 = rg ? Math.max(E[1], rg[1] - 1) : E[1];
    var ex1 = rg ? Math.min(E[2], rg[2] + 1) : E[2], ey1 = rg ? Math.min(E[3], rg[3] + 1) : E[3];
    if (ex1 > ex0 && ey1 > ey0)
      o += '<rect class="pcb" x="' + X(ex0) + '" y="' + Y(ey0) + '" width="' + f((ex1 - ex0) * k) + '" height="' + f((ey1 - ey0) * k) + '" rx="' + (rg ? 0 : f(1.5 * k)) + '"/>';
    B.forEach(function (fp) {
      if (/^MountingHole/.test(fp.f)) { var h = fp.p[0]; if (h && inR(h[0], h[1], 2)) o += '<circle class="lt" cx="' + X(h[0]) + '" cy="' + Y(h[1]) + '" r="' + f(1.6 * k) + '"/>'; return; }
      (fp.s || []).forEach(function (it) {
        if (it.c) { if (inR(it.c[0], it.c[1], it.c[2])) o += '<circle class="lt" style="stroke:var(--ink2)" cx="' + X(it.c[0]) + '" cy="' + Y(it.c[1]) + '" r="' + f(it.c[2] * k) + '"/>'; return; }
        var d = '', any = false;
        for (var i = 0; i < it.length; i += 2) { d += (i ? 'L' : 'M') + X(it[i]) + ' ' + Y(it[i + 1]); any = any || inR(it[i], it[i + 1], 3); }
        if (any) o += '<path class="lt" style="stroke:var(--ink2)" d="' + d + '"/>';
      });
      fp.p.forEach(function (q) {
        if (!inR(q[0], q[1], 2)) return;
        var w = (q[4] || 1.6) * k, hh = (q[5] || 1.6) * k;
        o += q[3] === 's' ? '<rect class="pad" x="' + f(m.ox + q[0] * k - w / 2) + '" y="' + f(m.oy + q[1] * k - hh / 2) + '" width="' + f(w) + '" height="' + f(hh) + '"/>'
                          : '<ellipse class="pad" cx="' + X(q[0]) + '" cy="' + Y(q[1]) + '" rx="' + f(w / 2) + '" ry="' + f(hh / 2) + '"/>';
        o += '<circle class="drill" cx="' + X(q[0]) + '" cy="' + Y(q[1]) + '" r="' + f(Math.min(w, hh) * 0.22) + '"/>';
      });
    });
    (I.text || []).forEach(function (t) {
      if (t.size < minText || !inR(t.x, t.y, 4)) return;
      var a = t.just === 'left' ? 'start' : t.just === 'right' ? 'end' : 'middle';
      o += T(X(t.x), Y(t.y + t.size * 0.35), t.s, { size: f(t.size * 0.9 * k), a: a, style: 'fill:var(--ink2);font-weight:600', tf: t.a ? 'rotate(' + (-t.a) + ' ' + X(t.x) + ' ' + Y(t.y) + ')' : '' });
    });
    return o;
  }
  FIG.pcb_probe = function (s, el) {
    var B = window.BOARD || [], I = window.BOARD_INFO || {};
    if (!B.length) return flat(s, 300, 30, T(150, 20, '[board data missing]'));
    var E = I.edge || [0, 0, 150, 100], k = 4, R = 66, kb = 15, colW = 200, gap = 26, top = 12, midX = (E[0] + E[2]) / 2;
    var BX = colW + gap, BW = (E[2] - E[0]) * k, BH = (E[3] - E[1]) * k, W = BX + BW + gap + colW;
    var X = function (x) { return BX + (x - E[0]) * k; }, Y = function (y) { return top + (y - E[1]) * k; };
    var probes = (el.getAttribute('data-probes') || '').split(';').filter(Boolean).map(function (e) {
      var p = e.split(':'), fp = B.find(function (b) { return b.r === p[1]; });
      var q = fp && fp.p.find(function (c) { return c[2] === p[2]; });
      if (!q) return null;
      var crowded = B.some(function (b) { return b.p.some(function (c) { return c !== q && Math.hypot(c[0] - q[0], c[1] - q[1]) < 3.3; }); });
      // every point gets a bubble (the user wants each lead shown up close, screws included)
      return { role: p[0], ref: p[1], pad: p[2], x: q[0], y: q[1], net: q[6] || '', zoom: crowded || true,
               label: p.slice(3).join(':') || (p[1] + ' pin ' + p[2] + (q[6] ? ' (' + q[6] + ')' : '')) };
    }).filter(Boolean);
    // the supply, if the test needs one, sits ABOVE the board with its leads dropping
    // into the pads named in data-supply="REF:pad REF:pad" (red, black), so they run
    // outside the board as far as they can
    var sup = (el.getAttribute('data-supply') || '').split(/\s+/).filter(Boolean).map(function (e) {
      var p = e.split(':'), fp = B.find(function (b) { return b.r === p[0]; }), q = fp && fp.p.find(function (c) { return c[2] === p[1]; });
      return q ? { x: q[0], y: q[1] } : null;
    });
    var hasSup = sup.length === 2 && sup[0] && sup[1];
    if (hasSup) top = 12 + 152;
    var g = boardMM(1.4, null, { ox: BX - E[0] * k, oy: top - E[1] * k, k: k });
    if (hasSup) {
      var pw = 150, mid = (X(sup[0].x) + X(sup[1].x)) / 2, px0 = Math.max(4, mid - pw / 2), by0 = 26, jy = by0 + 86, bot = by0 + 100;
      var set = el.getAttribute('data-supply-set') || '12.0 V, LIMIT 100 mA';
      g += '<rect class="k" x="' + f(px0) + '" y="4" width="' + pw + '" height="18" rx="3"/>' +
           T(f(px0 + pw / 2), 17, 'SET ' + set, { size: 9.5, style: 'fill:var(--paper);font-weight:800' });
      g += '<rect class="f" x="' + f(px0) + '" y="' + by0 + '" width="' + pw + '" height="100" rx="6"/>' +
           T(f(px0 + pw / 2), by0 + 15, 'DC POWER SUPPLY', { size: 10, style: 'font-weight:800' });
      // the displays show what the badge says: volts, and the limit in amps
      var mv = set.match(/([\d.]+)\s*V/), ma = set.match(/(\d+)\s*mA/), aa = set.match(/([\d.]+)\s*A\b/);
      var vs = mv ? (+mv[1]).toFixed(1) : '12.0', as = ma ? (+ma[1] / 1000).toFixed(2) : aa ? (+aa[1]).toFixed(2) : '0.10';
      // the two displays stacked, each named in full: VOLTS above AMPS (the current limit)
      [[by0 + 22, vs, 'VOLTS'], [by0 + 48, as, 'AMPS']].forEach(function (d_) {
        g += T(f(px0 + 12), d_[0] + 15, d_[2], { size: 10, a: 'start', style: 'font-weight:800' }) +
             '<rect class="disp" x="' + f(px0 + 62) + '" y="' + d_[0] + '" width="76" height="22" rx="2"/>' +
             T(f(px0 + 100), d_[0] + 16, d_[1], { size: 13, cls: 'mono' });
      });
      // jacks straight over their pads when red's pad is the left one (plain drops); spread
      // apart otherwise, so the swapped leads cross once, squarely, above the board
      var straight = X(sup[0].x) < X(sup[1].x);
      var jr = straight ? X(sup[0].x) : mid - 30, jb = straight ? X(sup[1].x) : mid + 30;
      [[sup[0], jr, bot + 10, PROBE.red, '+', -12], [sup[1], jb, bot + 22, PROBE.black, '−', 12]].forEach(function (L_) {
        var tx_ = X(L_[0].x), ty_ = Y(L_[0].y) - 1.5 * k;
        var d_ = Math.abs(L_[1] - tx_) < 0.5 ? 'M' + f(L_[1]) + ' ' + jy + 'V' + f(ty_)
                                            : 'M' + f(L_[1]) + ' ' + jy + 'V' + L_[2] + 'H' + f(tx_) + 'V' + f(ty_);
        g += '<path d="' + d_ + '" style="fill:none;stroke:' + L_[3] + ';stroke-width:2.6;stroke-linejoin:round"/>' +
             '<circle cx="' + f(L_[1]) + '" cy="' + jy + '" r="5" style="fill:' + L_[3] + '"/>' + T(f(L_[1] + L_[5]), jy + 4, L_[4], { size: 12, style: 'fill:' + L_[3] }) +
             '<circle cx="' + f(tx_) + '" cy="' + f(Y(L_[0].y)) + '" r="' + f(1.5 * k) + '" style="fill:none;stroke:' + L_[3] + ';stroke-width:2.4"/>';
      });
    }
    probes.forEach(function (p) {
      g += '<circle cx="' + f(X(p.x)) + '" cy="' + f(Y(p.y)) + '" r="' + f(1.5 * k) + '" style="fill:none;stroke:' + PROBE[p.role] + ';stroke-width:2.4"/>' +
           '<circle cx="' + f(X(p.x)) + '" cy="' + f(Y(p.y)) + '" r="' + f(0.45 * k) + '" style="fill:' + PROBE[p.role] + '"/>';
    });
    // the held-between part (1 kΩ, a wire): dashed line, labelled at its middle
    var br = probes.filter(function (p) { return p.role === 'bridge'; });
    for (var i = 0; i + 1 < br.length; i += 2) {
      var a = br[i], b = br[i + 1], mx = (X(a.x) + X(b.x)) / 2, my = (Y(a.y) + Y(b.y)) / 2;
      g += '<path d="M' + f(X(a.x)) + ' ' + f(Y(a.y)) + 'L' + f(X(b.x)) + ' ' + f(Y(b.y)) + '" style="fill:none;stroke:' + PROBE.bridge + ';stroke-width:2.2;stroke-dasharray:6 4"/>';
      g += '<rect class="f" x="' + f(mx - 24) + '" y="' + f(my - 9) + '" width="48" height="16" rx="3"/>' + T(f(mx), f(my + 3.5), el.getAttribute('data-bridge') || '1 kΩ', { size: 11 });
    }
    // group crowded points that sit together into one bubble
    var items = [];
    probes.forEach(function (p) {
      if (!p.zoom) { items.push({ kind: 'label', ps: [p], cx: p.x, cy: p.y }); return; }
      var c = items.find(function (it) { return it.kind === 'bubble' && Math.hypot(it.cx - p.x, it.cy - p.y) < 5.5; });
      if (c) { c.ps.push(p); c.cx = c.ps.reduce(function (s_, q) { return s_ + q.x; }, 0) / c.ps.length; c.cy = c.ps.reduce(function (s_, q) { return s_ + q.y; }, 0) / c.ps.length; }
      else items.push({ kind: 'bubble', ps: [p], cx: p.x, cy: p.y });
    });
    items.forEach(function (it) { it.h = it.kind === 'bubble' ? 2 * R + 26 + it.ps.length * 16 : 22; it.side = it.cx < midX ? 'L' : 'R'; });
    var H = top + BH + 12;
    ['L', 'R'].forEach(function (side) {
      var col = items.filter(function (it) { return it.side === side; }).sort(function (a, b) { return a.cy - b.cy; });
      var ys = col.map(function (it) { return Math.max(4, Y(it.cy) - (it.kind === 'bubble' ? R + 16 : 7)); });
      for (var j = 1; j < ys.length; j++) ys[j] = Math.max(ys[j], ys[j - 1] + col[j - 1].h + 8);
      col.forEach(function (it, j) {
        var y0 = ys[j], cxCol = side === 'L' ? colW / 2 : BX + BW + gap + colW / 2;
        H = Math.max(H, y0 + it.h + 4);
        if (it.kind === 'label') {
          var p = it.ps[0], tx = side === 'L' ? 6 : BX + BW + gap + 6, ty = y0 + 7, mx = X(p.x), my = Y(p.y);
          g += '<circle cx="' + (tx + 5) + '" cy="' + ty + '" r="5" style="fill:' + PROBE[p.role] + '"/>' + T(tx + 15, ty + 4.5, p.label, { size: 12.5, a: 'start' });
          var ex = side === 'L' ? Math.min(tx + 20 + p.label.length * 6.9, colW + gap - 4) : tx - 3;
          g += '<path class="t" d="M' + f(ex) + ' ' + f(ty) + 'H' + f(mx) + 'V' + f(my + (ty < my ? -6 : 6)) + '"/>';
          return;
        }
        // bubble: the same board, magnified, clipped to a circle
        var bx = cxCol, by = y0 + 16 + R, id = 'pbz' + (++uid), mxc = X(it.cx), myc = Y(it.cy);
        var cap = it.ps.every(function (q) { return q.role === 'pin'; }) ? '' :
                  it.ps.every(function (q) { return q.role === 'bridge'; }) ? 'Hold the resistor lead here' : 'Place the DVM Leads Here';
        if (cap) g += T(f(bx), f(y0 + 10), cap, { size: 11.5, style: 'font-weight:800' });
        var spread = it.ps.reduce(function (m, q) { return Math.max(m, Math.hypot(q.x - it.cx, q.y - it.cy)); }, 0) + 3;
        var dx = bx - mxc, dy = by - myc, d = Math.hypot(dx, dy) || 1;
        g += '<circle cx="' + f(mxc) + '" cy="' + f(myc) + '" r="' + f(spread * k) + '" style="fill:none;stroke:var(--ink);stroke-width:1.4"/>';
        // the leader is square, like every other indicator line: across at the bubble's height, then up or down to the ring
        var rr = spread * k, sx = bx + (side === 'L' ? R : -R);
        if (Math.abs(by - myc) < rr * 0.7) g += '<path class="l" d="M' + f(sx) + ' ' + f(by) + 'H' + f(mxc + (side === 'L' ? -1 : 1) * Math.sqrt(Math.max(0, rr * rr - (by - myc) * (by - myc)))) + '"/>';
        else g += '<path class="l" d="M' + f(sx) + ' ' + f(by) + 'H' + f(mxc) + 'V' + f(myc + (by < myc ? -rr : rr)) + '"/>';
        g += '<clipPath id="' + id + '"><circle cx="' + f(bx) + '" cy="' + f(by) + '" r="' + R + '"/></clipPath>';
        g += '<g clip-path="url(#' + id + ')"><rect x="' + f(bx - R) + '" y="' + f(by - R) + '" width="' + 2 * R + '" height="' + 2 * R + '" style="fill:var(--paper)"/>' +
             boardMM(0, [it.cx - R / kb, it.cy - R / kb, it.cx + R / kb, it.cy + R / kb], { ox: bx - it.cx * kb, oy: by - it.cy * kb, k: kb });
        // a probe tip on each point, its body pointing away from the pin names
        // printed nearby and from the other probes, so nothing is hidden or crossed
        var texts = (I.text || []).map(function (t) {
          var ang = -t.a * Math.PI / 180, len = String(t.s).length * t.size * 0.3, sg = t.just === 'left' ? 1 : t.just === 'right' ? -1 : 0;
          return [t.x + sg * len * Math.cos(ang), t.y + sg * len * Math.sin(ang)];
        });
        it.ps.forEach(function (p) {
          if (p.role === 'pin') {                               // a hole to find, not a probe: a ring and an arrow
            var qx = bx + (p.x - it.cx) * kb, qy = by + (p.y - it.cy) * kb;
            g += '<circle cx="' + f(qx) + '" cy="' + f(qy) + '" r="' + f(kb * 0.95) + '" style="fill:none;stroke:' + PROBE.pin + ';stroke-width:3"/>';
            return;
          }
          var best = null, oth = it.ps.filter(function (o) { return o !== p; });
          var awx = oth.reduce(function (s_, o) { return s_ + p.x - o.x; }, 0), awy = oth.reduce(function (s_, o) { return s_ + p.y - o.y; }, 0);
          for (var a8 = 0; a8 < 8; a8++) {
            var th = -Math.PI / 4 - a8 * Math.PI / 4, ux = Math.cos(th), uy = Math.sin(th), pen = a8 * 0.01;
            if (oth.length && ux * awx + uy * awy <= 0.05 * Math.hypot(awx, awy)) continue;   // tips splay apart, never cross
            texts.forEach(function (c) {
              var vx = c[0] - p.x, vy = c[1] - p.y, dd = Math.hypot(vx, vy);
              if (dd > 0.01 && dd < 4) pen += Math.max(0, (ux * vx + uy * vy) / dd);
            });
            it.ps.forEach(function (o) {
              if (o === p) return;
              var vx = o.x - p.x, vy = o.y - p.y, dd = Math.hypot(vx, vy);
              if (dd > 0.01) pen += 2.5 * Math.max(0, (ux * vx + uy * vy) / dd + 0.3);
            });
            if (!best || pen < best.pen) best = { pen: pen, ux: ux, uy: uy };
          }
          var px = bx + (p.x - it.cx) * kb, py = by + (p.y - it.cy) * kb;
          g += '<path d="M' + f(px) + ' ' + f(py) + 'l' + f(best.ux * 10) + ' ' + f(best.uy * 10) + '" style="stroke:var(--ink);stroke-width:2;stroke-linecap:round"/>' +
               '<path d="M' + f(px + best.ux * 10) + ' ' + f(py + best.uy * 10) + 'l' + f(best.ux * 46) + ' ' + f(best.uy * 46) + '" style="stroke:' + PROBE[p.role] + ';stroke-width:7;stroke-linecap:round"/>' +
               '<circle cx="' + f(px) + '" cy="' + f(py) + '" r="' + f(kb * 0.9) + '" style="fill:none;stroke:' + PROBE[p.role] + ';stroke-width:2.4"/>';
        });
        g += '</g><circle cx="' + f(bx) + '" cy="' + f(by) + '" r="' + R + '" style="fill:none;stroke:var(--ink);stroke-width:2"/>';
        it.ps.forEach(function (p, n) {
          var ty = by + R + 18 + n * 16;
          g += '<circle cx="' + f(bx - 86) + '" cy="' + f(ty - 4) + '" r="5" style="fill:' + PROBE[p.role] + '"/>' + T(f(bx - 76), f(ty), p.label, { size: 12, a: 'start' });
        });
      });
    });
    return flat(s, W, H, g);
  };

  /* ---------------- bench power supply ---------------- */
  /* psu: a bench supply set up for a test, leads to the board. data-v (volts),
     data-i (current limit, as shown), data-a / data-b (where red and black go),
     data-set (the badge text, default "SET  <v> V, LIMIT <i>") */
  FIG.psu = function (s, el) {
    var v = el.getAttribute('data-v') || '12.0', i = el.getAttribute('data-i') || '0.100';
    var a = el.getAttribute('data-a') || '+', b = el.getAttribute('data-b') || 'GND';
    var set = el.getAttribute('data-set') || ('SET  ' + v + ' V,  LIMIT ' + (+i < 1 ? Math.round(+i * 1000) + ' mA' : i + ' A'));
    var RED = '#d42a22', g = '';
    g += '<rect class="f" x="10" y="8" width="230" height="150" rx="8"/>';
    g += T(110, 26, 'DC POWER SUPPLY', { size: 11, style: 'font-weight:800' });
    [[40, v, 'VOLTS'], [86, i, 'AMPS']].forEach(function (d) {     // stacked, named in full
      g += T(26, d[0] + 24, d[2], { size: 12, a: 'start', style: 'font-weight:800' }) +
           '<rect class="disp" x="86" y="' + d[0] + '" width="104" height="34" rx="2"/>' + T(138, d[0] + 24, d[1], { size: 18, cls: 'mono' });
    });
    // output jacks stacked at the right edge, red above black, so the leads leave without crossing anything
    g += '<circle cx="220" cy="96" r="6" style="fill:' + RED + '"/><circle class="k" cx="220" cy="132" r="6"/>' +
         T(220, 84, '+', { size: 12, style: 'fill:' + RED }) + T(220, 120, '−', { size: 12 });
    g += '<rect class="k" x="10" y="166" width="230" height="22" rx="4"/>' + T(125, 182, set, { size: 11.5, style: 'fill:var(--paper);font-weight:800' });
    g += '<path class="probe" style="stroke:' + RED + '" d="M220 96 C248 96 244 46 268 46"/><path class="probe k2" d="M220 132 C244 132 248 134 268 134"/>';
    g += '<path d="M268 42 l26 4 l-26 4z" style="fill:' + RED + '"/><path class="k" d="M268 130 l26 4 l-26 4z"/>';
    g += T(300, 40, a, { a: 'start', size: 12 }) + T(300, 128, b, { a: 'start', size: 12 });
    g += T(300, 58, 'red lead, +', { a: 'start', size: 10, style: 'fill:var(--ink2);font-weight:500' }) + T(300, 146, 'black lead, −', { a: 'start', size: 10, style: 'fill:var(--ink2);font-weight:500' });
    return flat(s, 420, 194, g);
  };

  /* ---------------- resistor colour bands ---------------- */
  var BAND = ['#151515', '#7a4520', '#d42a22', '#f07d10', '#f3cf00', '#2c9a44', '#2563c9', '#7d4cc0', '#8b8b8b', '#f5f5f5'];
  var BNAME = ['black', 'brown', 'red', 'orange', 'yellow', 'green', 'blue', 'violet', 'grey', 'white'];
  var GOLD = '#c9a227';
  function ohms(v) { var m = String(v).match(/^(\d+(?:\.\d+)?)([kKM]?)/); return m ? +m[1] * ({ k: 1e3, K: 1e3, M: 1e6 }[m[2]] || 1) : 0; }
  function bandsFor(r, nd) {
    var e = Math.floor(Math.log10(r) + 1e-9) - (nd - 1), sig = Math.round(r / Math.pow(10, e));
    if (sig >= Math.pow(10, nd)) { sig /= 10; e += 1; }
    var d = String(sig).split('').map(Number);
    return { digits: d, mult: e };
  }
  function resistor(x, y, cols, body) {
    var o = lead(x - 26, y, x, y) + lead(x + 96, y, x + 122, y);
    o += '<rect x="' + x + '" y="' + (y - 11) + '" width="96" height="22" rx="10" style="fill:' + body + ';stroke:var(--ink);stroke-width:1.2"/>';
    var xs = cols.length === 4 ? [16, 30, 44, 76] : [14, 26, 38, 50, 78];
    cols.forEach(function (c, i) { o += '<rect x="' + (x + xs[i]) + '" y="' + (y - 11) + '" width="7" height="22" style="fill:' + c + ';stroke:var(--ink);stroke-width:.5"/>'; });
    return o;
  }
  FIG.res_bands = function (s, el) {
    var vals = (el.getAttribute('data-values') || '10k').split(/\s+/), g = '', y = 30;
    g += T(110, 14, 'ordinary  (4 bands, 5 %)', { size: 11.5, style: 'fill:var(--ink2)' }) + T(375, 14, 'precision  (5 bands, 1 %)', { size: 11.5, style: 'fill:var(--ink2)' });
    vals.forEach(function (v) {
      var only1 = /:1%$/.test(v), name = v.replace(/:1%$/, ''), r = ohms(name), yy = y + 22;
      g += T(6, yy + 5, pretty(name) + (only1 ? ' 1%' : ''), { size: 15, a: 'start', cls: 'tx cnt' });
      var mc = function (e) { return e >= 0 ? BAND[e] : e === -1 ? GOLD : '#b9bdc1'; };
      var mn = function (e) { return e >= 0 ? BNAME[e] : e === -1 ? 'gold' : 'silver'; };
      if (!only1) {
        // two digits, then the multiplier (a count of zeros), then tolerance
        var b4 = bandsFor(r, 2), c4 = [BAND[b4.digits[0]], BAND[b4.digits[1]], mc(b4.mult), GOLD];
        g += resistor(122, yy, c4, '#e8d5ad');
        g += T(170, yy + 26, [BNAME[b4.digits[0]], BNAME[b4.digits[1]], mn(b4.mult), 'gold'].join(' · '), { size: 10 });
      } else g += T(170, yy + 4, '—', { size: 14, style: 'fill:var(--ink2)' });
      var b5 = bandsFor(r, 3);
      var c5 = [BAND[b5.digits[0]], BAND[b5.digits[1]], BAND[b5.digits[2]], mc(b5.mult), BAND[1]];
      g += resistor(327, yy, c5, '#a9cdee');
      g += T(375, yy + 26, [BNAME[b5.digits[0]], BNAME[b5.digits[1]], BNAME[b5.digits[2]], mn(b5.mult), 'brown'].join(' · '), { size: 10 });
      y += 56;
    });
    return flat(s, 480, y + 4, g);
  };

  /* ---------------- ceramic capacitor code ---------------- */
  FIG.cap_code = function (s, el) {
    var code = el.getAttribute('data-code') || '104', d = +code.slice(0, 2), z = +code.slice(2), pf = d * Math.pow(10, z);
    var nf = pf / 1000, uf = pf / 1e6, g = '';
    g += lead(46, 66, 46, 104) + lead(70, 66, 70, 104) + '<ellipse class="f" cx="58" cy="44" rx="34" ry="30"/>' + T(58, 51, code, { size: 19, cls: 'mono' });
    g += T(58, 122, 'no + or −', { size: 11 });
    var x = 120, row = function (a, b, y) { g += T(x, y, a, { size: 14, a: 'start', cls: 'mono' }) + T(x + 150, y, b, { size: 12, a: 'start', style: 'fill:var(--ink2)' }); };
    row(code.slice(0, 2) + ' then ' + z + ' zero' + (z === 1 ? '' : 's'), 'the first two digits, then a count of zeros', 28);
    row(String(pf).replace(/\B(?=(\d{3})+(?!\d))/g, ' ') + ' pF', 'picofarads', 54);
    row('= ' + nf + ' nF', 'nanofarads: the value in the parts list', 80);
    row('= ' + uf + ' µF', 'microfarads: how some sellers list it', 106);
    return flat(s, 520, 130, g);
  };

  /* ---------------- male and female ---------------- */
  FIG.male_female = function (s) {
    var g = '', i;
    // male header: pins up
    g += '<rect class="k" x="20" y="96" width="120" height="14"/>';
    for (i = 0; i < 5; i++) g += '<rect class="m" x="' + (31 + i * 24) + '" y="40" width="6" height="88"/>';
    g += T(80, 150, 'MALE header', { size: 14, cls: 'tx cnt' }) + T(80, 167, 'pins stick up', { size: 11.5 });
    // female socket: holes
    g += '<rect class="f" x="200" y="74" width="120" height="36"/>';
    for (i = 0; i < 5; i++) g += '<rect class="k" x="' + (208 + i * 24) + '" y="80" width="8" height="8"/>' + lead(212 + i * 24, 110, 212 + i * 24, 128);
    g += T(260, 150, 'FEMALE socket', { size: 14, cls: 'tx cnt' }) + T(260, 167, 'holes take pins', { size: 11.5 });
    // a module's male pins into a female socket
    g += '<rect class="f" x="380" y="14" width="130" height="24" rx="3"/>' + T(445, 31, 'module', { size: 11 });
    for (i = 0; i < 5; i++) g += '<rect class="m" x="' + (393 + i * 24) + '" y="38" width="5" height="26"/>';
    g += '<path class="ar" d="M445 72 l-8 -10 h5 v-12 h6 v12 h5z"/>';
    g += '<rect class="f" x="380" y="74" width="130" height="36"/>';
    for (i = 0; i < 5; i++) g += '<rect class="k" x="' + (391 + i * 24) + '" y="80" width="8" height="8"/>' + lead(395 + i * 24, 110, 395 + i * 24, 128);
    g += T(445, 150, 'module pins go IN', { size: 14, cls: 'tx cnt' }) + T(445, 167, 'the socket stays on the board', { size: 11.5 });
    g += '<rect class="pcb" x="10" y="128" width="510" height="6"/>';
    return flat(s, 530, 176, g);
  };
  FIG.header_snap = function (s) {
    var g = '<rect class="k" x="10" y="40" width="300" height="12"/>', i;
    for (i = 0; i < 12; i++) g += '<rect class="m" x="' + (19 + i * 25) + '" y="14" width="5" height="62"/>';
    g += '<path class="cut" d="M146.5 4 V88"/>' + T(150, 104, 'cut through pin 6', { size: 11.5, a: 'start' });
    g += T(66, 104, '5 pins', { size: 11.5 }) + T(330, 50, '…', { size: 18, a: 'start' });
    g += '<path class="l" d="M16 92 h104"/><path class="l" d="M16 88 v8 M120 88 v8"/>';
    return flat(s, 350, 112, g);
  };

  /* ---------------- soldering ---------------- */
  function boardSec(x, y, w) {
    // board cross-section with a plated hole at x
    return '<rect class="pcb" x="' + (x - w / 2) + '" y="' + y + '" width="' + w + '" height="16"/>' +
      '<rect class="pad" x="' + (x - 22) + '" y="' + (y - 3) + '" width="44" height="3"/><rect class="pad" x="' + (x - 22) + '" y="' + (y + 16) + '" width="44" height="3"/>' +
      '<rect class="drill" x="' + (x - 7) + '" y="' + y + '" width="14" height="16"/>';
  }
  function joint(kind) {
    var x = 80, y = 70, g = boardSec(x, y, 140);
    g += '<rect class="m" x="' + (x - 3) + '" y="' + (kind === 'bridge' ? 10 : 12) + '" width="6" height="' + (y + 40 - 12) + '"/>';
    if (kind === 'ok') g += '<path class="m" d="M' + (x - 21) + ' ' + (y - 3) + ' Q' + (x - 6) + ' ' + (y - 6) + ' ' + (x - 3) + ' ' + (y - 26) + ' H' + (x + 3) + ' Q' + (x + 6) + ' ' + (y - 6) + ' ' + (x + 21) + ' ' + (y - 3) + ' Z"/>';
    if (kind === 'blob') g += '<ellipse class="m" cx="' + x + '" cy="' + (y - 18) + '" rx="22" ry="18"/>';
    if (kind === 'cold') g += '<path class="m" d="M' + (x - 14) + ' ' + (y - 9) + ' Q' + x + ' ' + (y - 34) + ' ' + (x + 14) + ' ' + (y - 9) + ' Z"/><path class="t" d="M' + (x - 22) + ' ' + (y - 6) + ' h7 M' + (x + 15) + ' ' + (y - 6) + ' h7"/>';
    if (kind === 'bridge') {
      g += boardSec(x + 50, y, 0).replace(/<rect class="pcb"[^>]*>/, '') + '<rect class="m" x="' + (x + 47) + '" y="12" width="6" height="98"/>';
      g += '<path class="m" d="M' + (x - 20) + ' ' + (y - 3) + ' Q' + (x + 25) + ' ' + (y - 34) + ' ' + (x + 70) + ' ' + (y - 3) + ' Z"/>';
    }
    return g;
  }
  FIG.joint_ok = function (s) { return flat(s, 160, 118, joint('ok')); };
  FIG.joint_blob = function (s) { return flat(s, 160, 118, joint('blob')); };
  FIG.joint_cold = function (s) { return flat(s, 160, 118, joint('cold')); };
  FIG.joint_bridge = function (s) { return flat(s, 160, 118, joint('bridge')); };
  FIG.solder_how = function (s) {
    var x = 190, y = 110, g = boardSec(x, y, 300);
    g += '<rect class="m" x="' + (x - 3) + '" y="40" width="6" height="96"/>';
    // iron from the left, tip touching pad and lead together
    g += '<path class="f" d="M40 30 L150 92 L144 101 L34 40 Z"/><path class="m" d="M150 92 L184 106 L178 108 L144 101 Z"/>';
    // solder wire from the right, onto the far side of the joint
    g += '<path class="lead" d="M360 74 C320 76 240 80 198 104" style="stroke-width:3.2"/>';
    g += T(80, 20, '1  iron on pad AND lead', { size: 12, a: 'start' }) + T(240, 30, '2  solder to the joint,', { size: 12, a: 'start' }) + T(252, 46, 'not to the iron', { size: 12, a: 'start' });
    g += T(190, 156, '3  solder off, then iron off: about 3 seconds in all', { size: 12 });
    return flat(s, 380, 166, g);
  };
  FIG.bend_lead = function (s) {
    var g = '<rect class="pcb" x="20" y="60" width="260" height="16"/>';
    g += '<rect class="f" x="112" y="22" width="76" height="22" rx="10"/>' + lead(124, 44, 124, 60) + lead(176, 44, 176, 60);
    g += '<path class="lead" d="M124 76 V84 L100 104"/><path class="lead" d="M176 76 V84 L200 104"/>';
    g += T(150, 122, 'splay the legs under the board so the part cannot fall out', { size: 11.5 });
    g += T(290, 72, 'board', { size: 10, a: 'end', style: 'fill:var(--ink2)' });
    return flat(s, 300, 130, g);
  };
})();
