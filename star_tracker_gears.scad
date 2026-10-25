// =====================================================================
//  star_tracker_gears.scad
//  Parametric involute gear train for a sidereal-rate camera tracker.
//
//  Self-contained -- no libraries. Needs OpenSCAD 2021.01+ (offset()).
//
//  TWO BUILDS share stages 1-3 (15/120 module 1.0, 8:1 each = 512:1):
//
//  (A) all printed      final 15/135 module 1.0  ->  4608:1
//        200-step motor @ 1/16  ->  14,745,600 usteps/sidereal day
//        5843.38 us/step, motor 3.209 rpm, 0.088 arcsec/ustep
//        parts: 3 x "stage", 1 x "output", 1 x "pinion"
//
//  (B) laser final      final 24/96  module 2.0  ->  2048:1
//        200-step motor @ 1/32  ->  13,107,200 usteps/sidereal day
//        6573.80 us/step, motor 1.426 rpm, 0.099 arcsec/ustep
//        parts: 2 x "stage", 1 x "finalstage", 1 x "pinion",
//               + "lasercut" SVG sent out for cutting
//
//  (B) exists because module-1 tooth tips are 0.69 mm -- too fine for a
//  laser. Module 2.0 gives 1.43 mm tips AND triples the final pinion
//  radius, which is what actually sets short-exposure trailing:
//      excursion over t  =  (e / r_pinion) * 2*pi*t / 86164
// =====================================================================

/* [What to render] */
// train = layout check, plate = ready to slice, test_pair = check the mesh
part = "test_pair";  // [train, plate, stage, output, pinion, testwheel, testplate, fitgauge, boregauge, spacers, assembly, encoder, tower, hublower, hubupper, magnetcap, as5600bracket, bearinggauge, baseplate, baseplatecut, towerlower, towerupper, towerdowels, towersplit, finalwheel, finalstage, finalpinion, laserwheel, lasercut, dxf, test_pair]

/* [Gear cutting] */
mod_      = 1.0;    // transverse module, mm
pa        = 20;     // pressure angle, deg
backlash  = 0.25;   // total backlash per mesh, mm (0.15 tight .. 0.35 loose)
clear_    = 0.25;   // root clearance, x module
fillet    = 0.35;   // root fillet radius, mm (0 = off)
helix     = 0;      // helix angle, deg. 0 = spur. Try 15 for quiet running.
flank_pts = 14;     // involute sample points per flank

/* [Tooth counts] */
zp = 15;            // pinion, every stage
zw = 120;           // wheel, stages 1-3
zf = 135;           // final wheel

/* [Sizes] */
fw_wheel  = 5;      // wheel face width
fw_pinion = 7;      // pinion face width -- 2 mm MORE than fw_wheel on purpose,
                    // so consecutive wheels can be axially separated by z_gap
                    // while the pinion still covers the full wheel face
z_gap     = 1.5;    // axial gap between consecutive wheels in the stack
hub_d     = 12;     // hub boss diameter
lighten   = true;   // lightening holes in the big wheels

/* [Centre mount] */
// How the part attaches to its shaft.
//   round    - plain through hole
//   dshaft   - round hole with a flat, for a NEMA17 D-cut shaft
//   hex      - hex socket, traps an M5 nut so an M5 bolt becomes the shaft
//   bearing  - through hole + press-fit pocket for a ball bearing
//   thread   - undersize hole to thread-form a bolt straight into plastic
mount_type = "round";  // [round, dshaft, hex, bearing, thread]
out_mount  = "round";  // [round, dshaft, hex, bearing, thread] - output wheel only

bore    = 5.2;   // 5 mm shaft, printed loose. M5 bolt = 5.3
d_flat  = 2.1;   // axis to the D flat. NEMA17 is 2.0 + 0.1 clearance
hex_af  = 8.2;   // across flats. M5 nut = 8.0 + 0.2
brg_od  = 16.0;  // bearing outer race. 625ZZ = 16, 608ZZ = 22
brg_id  = 5.0;   // bearing bore (only used to size the shaft clearance)
brg_w   = 5.0;   // bearing width. 625ZZ = 5, 608ZZ = 7
thread_d = 5.5;  // 1/4"-20 formed into plastic. M5 = 4.6, M6 = 5.5

/* [Laser-cut final stage] */
// Coarse module so the tooth tips survive a laser. Short-exposure trailing
// scales as 1/r_pinion, so the big final pinion is the real win here --
// 24T at module 2 has r=24 mm against the old 15T at module 1's r=7.5 mm.
mod_f    = 2.0;   // final stage module
zpf      = 24;    // final pinion teeth  (PRINTED, sits on shaft 3)
zwf      = 96;    // final wheel teeth   (LASER CUT)  -> 4:1
lw_bore  = 20;    // central clearance hole in the laser wheel
lw_bolts = 6;     // clamp bolts holding it to the printed carrier
lw_bc_r  = 35;    // clamp bolt circle radius
lw_bc_d  = 3.2;   // M3 clearance

/* [Backlash test jig] */
tp_zw       = 30;    // wheel teeth used on the test plate
tp_post_d   = 4.70;  // SHAFT POST DIAMETER -- measured on the fit gauge
                     // 4.70 post <-> 5.20 nominal bore = 0.50 mm total
                     // hole-shrink + post-growth on this printer/filament.
fg_d0       = 4.70;  // fit gauge: smallest stub
fg_step     = 0.10;  // fit gauge: step
fg_n        = 7;     // fit gauge: number of stubs
bg_d0       = 5.00;  // bore gauge: smallest hole
bg_step     = 0.10;  // bore gauge: step
bg_n        = 7;     // bore gauge: number of holes
bg_t        = 6;     // bore gauge thickness (~ a gear face width)
bpg_d0      = 16.0;  // bearing gauge: smallest pocket (625ZZ is 16.0 nominal)
bpg_step    = 0.1;
bpg_n       = 8;
bpg_t       = 8;     // bearing gauge bar thickness
tp_plate_t  = 4;     // plate thickness
tp_collar_d = 11;    // collar the gear rides on, clear of the plate face
tp_collar_h = 1.2;
tp_screw_d  = 2.7;   // M3 thread-forming, retains the keeper washer

/* [Shaft spacers] */
// Each gear is set to its Z height by a spacer stack under it. The spacer OD
// is the TIGHTEST fit in the whole gearbox: a spacer on shaft 2 shares a Z
// band with shaft 1's 120T wheel, 67.5 mm away with a 61 mm tip radius, so
// its radius cannot exceed 6.5 mm. 13 mm OD is the hard wall; 10 mm leaves
// 1.5 mm of air. Same story for shaft 3 against shaft 2's wheel.
sp_od  = 10;   // spacer outside diameter -- HARD MAX 13
sp_clr = 2;    // running clearance between baseplate and the lowest gear
explode = 0;   // assembly view only: lift every gear off its spacer, mm

/* [Output shaft and AS5600 encoder] */
// The output is the ONE shaft that rotates: the magnet must sit on the axis
// and turn with the wheel, and the camera load wants real bearings anyway.
os_shaft_d = 5.0;    // ground shaft, not a nail
os_brg_od  = 16;     // 625ZZ outer race
os_brg_w   = 5;      // 625ZZ width
os_tower_od = 22;    // bearing tower OD -- also the sensor bracket's register
os_hub_od  = 44;     // clamp hubs; must exceed 2*bc_r so the bolts land in meat
os_reg_d   = 22;     // register the wheel is located on
os_mag_d   = 6;      // diametric magnet
os_mag_h   = 2.5;
os_flange_d = 44;    // tower flange. Sized by the bolt circle below, then
                     // checked against shaft 1's wheel: 85.5 - 61 - 22 = 2.5 mm
os_flange_bc = 35;   // flange bolt circle dia. At Ø28 the web between the
                     // plate's Ø22.4 spigot bore and each M3 was only 1.10 mm
                     // -- under the min-feature rule for 4 mm aluminium.
                     // Ø35 gives 4.60 mm.
os_brg_fit = 0.5;    // bearing pocket allowance -- SET THIS FROM THE BEARING GAUGE
os_bkt_fit = 0.5;    // AS5600 bracket sleeve over the 22 mm tower tube. The printer's
                     // post-under-hole rule (build.ps1) wants 0.5; the old 0.3 was tight.
os_cut     = false;  // cut the assembly in half for a section view (needs --render)

/* [Split bearing tower] */
// The one-piece tower will not print, so it is two halves that CLAMP THE
// BASEPLATE between them. Rev A split at z = 0 with a 45 deg cone under the
// flange, but that cone sat inside the 4.3 mm plate (32-44 mm across where the
// plate hole is 22.4) and the flange could never seat.
//   upper half: flange on the plate top + tube, z 0 .. 14
//   lower half: clamp disc against the plate underside + tube, z -21 .. -bp_t
// Nothing of either half is inside the plate hole; the dowels bridge that gap.
// Both halves print with their plate-facing face on the bed, and each one only
// gets narrower going up the print, so neither has an overhang. The lower
// disc's underside is also the AS5600 bracket's axial stop.
os_disc_d   = 41;    // lower clamp disc. Max ~41: the wedge adapter's M5s sit
                     // at r 25 and need adapter material between them and it
os_disc_t   = 6.5;   // thick enough for M3x14 to thread-form 5.7 mm into it
os_pin_n    = 3;     // alignment dowels
os_pin_r    = 8;     // dowel circle radius (clear of the bore and the M3s)
os_pin_d    = 3.2;   // socket dia
os_pin_len  = 4.0;   // socket depth into EACH half; the dowel also spans bp_t
os_exp     = 0;      // exploded view: axial separation between parts, mm
os_shaft_len = 55;   // ground shaft cut to this. Bottom bottoms in the magnet
                     // cap at z -33; top at z 22 clears the hub grub (z 17.5)
                     // and stays under the camera nut (z 26.5)
os_washer_t = 1.0;   // printed washer on the upper bearing's INNER race, so the
os_washer_od = 7.2;  // turning hub never touches the tower rim or outer race

/* [Baseplate] */
// A real part, not a render stub. It only has to carry the five shafts and
// the motor -- the wheels overhang it -- so it is a hull of lobes at the
// shaft centres rather than a slab under the whole footprint.
bp_t          = 4.3;   // AS ORDERED from SendCutSend (nearest stock to 4.0)
bp_shaft_d    = 5.5;   // shafts 1-3 are #12 screws, nutted under the plate. As built:
                       // the 4.9 nail press holes were drilled out to 5.5 by hand.
bp_motor_boss = 23;    // NEMA17 pilot boss registers here
bp_motor_bc   = 31;    // NEMA17 bolt pattern, square
bp_tower_bc   = 35;    // bearing tower flange bolts, dia -- must equal os_flange_bc
bp_tower_d    = 22.4;  // tower spigot clearance at shaft 4
bp_mount_bc   = 50;    // wedge/tripod bolt circle, centred on the OUTPUT axis
bp_mount_d    = 5.5;   // M5 clearance
bp_mount_n    = 4;
bp_lobe       = [26, 20, 20, 20, 34];  // outline radius at each shaft

/* [Mounting bolt circle] */
bc_holes = 4;      // number of holes, 0 = none
bc_r     = 12;     // bolt circle radius, mm
bc_d     = 3.2;    // M3 clearance. 2.7 to thread-form instead
bc_cbore = false;  // counterbore for M3 socket-head caps
bc_on    = "output";  // [output, all, none] - which parts get the bolt circle

$fn = 72;

// ---------------------------------------------------------------------
//  involute geometry
// ---------------------------------------------------------------------
// Every function takes an optional module `m`, so ONE compound gear can carry
// a module-1 wheel and a module-2 pinion. Defaults to the global mod_.
function g_r (z, m=mod_) = m*z/2;                   // pitch radius
function g_rb(z, m=mod_) = g_r(z,m)*cos(pa);        // base radius
function g_ra(z, m=mod_) = g_r(z,m) + m;            // addendum (tip) radius
function g_rf(z, m=mod_) = g_r(z,m) - m*(1+clear_); // dedendum (root) radius

function inv_d(a) = (tan(a) - a*PI/180) * 180/PI;   // involute fn, deg in/out

// half tooth thickness at the pitch circle, degrees, backlash removed
function g_psi(z, m=mod_) = 90/z - backlash*90/(PI*m*z);

// angular position of the flank at radius R (radial below the base circle)
function flank_a(z, R, m=mod_) =
    g_psi(z,m) + inv_d(pa) - inv_d(acos(g_rb(z,m)/max(R, g_rb(z,m))));

function pol(R,t) = [R*cos(t), R*sin(t)];

function radii(z, m=mod_) = let(rs = g_rf(z,m)*0.90, ra = g_ra(z,m))
                    [for (i=[0:flank_pts]) rs + (ra-rs)*i/flank_pts];

// one tooth: up the right flank, across the tip, back down the left flank
function tooth(z, m=mod_) = let(rr = radii(z,m), n = len(rr))
    concat([for (i=[0:n-1])    pol(rr[i], -flank_a(z, rr[i], m))],
           [for (i=[n-1:-1:0]) pol(rr[i],  flank_a(z, rr[i], m))]);

// tooth tip width in mm -- the number a laser cutter cares about
function tip_w(z, m=mod_) =
    2 * flank_a(z, g_ra(z,m), m) * PI/180 * g_ra(z,m);

// twist in degrees over face width h, so that BOTH gears of a pair share
// the same helix angle at their (different) pitch radii.  Because we cut in
// the transverse plane, centre distance stays exactly mod_*(z1+z2)/2.
function twist_of(z, h, m=mod_) = (h*tan(helix)/g_r(z,m)) * 180/PI;

function cdist(z1, z2, m=mod_) = m*(z1+z2)/2;

// ---------------------------------------------------------------------
//  2D / 3D gear bodies
// ---------------------------------------------------------------------
module gear_raw(z, m=mod_) {
    union() {
        circle(r = g_rf(z,m), $fn = max(64, z*4));
        for (i=[0:z-1]) rotate([0,0, i*360/z]) polygon(tooth(z,m));
    }
}

// morphological closing puts a fillet in every root
module gear2d(z, m=mod_) {
    if (fillet > 0) offset(r=-fillet) offset(r=fillet) gear_raw(z,m);
    else gear_raw(z,m);
}

module gear3d(z, h, hand=1, m=mod_) {
    tw = twist_of(z,h,m) * hand;
    linear_extrude(height=h, twist=tw, convexity=10,
                   slices = (helix == 0) ? 1 : max(2, ceil(abs(tw)/2)))
        gear2d(z,m);
}

// ---------------------------------------------------------------------
//  features
// ---------------------------------------------------------------------
// keep clear of both the hub and the bolt circle
function web_r() = max(hub_d/2,
                       (mount_type == "bearing" || out_mount == "bearing")
                            ? brg_od/2 + 2.5 : 0,
                       (bc_holes > 0) ? bc_r + bc_d : 0);

module lightening_2d(z, m=mod_) {
    if (lighten && z >= 60) {
        ri = web_r() + 5;
        ro = g_rf(z,m) - 7;
        rr = (ri+ro)/2;
        dd = (ro-ri)*0.78;
        if (ro > ri + 6)
            for (i=[0:5]) rotate([0,0, i*60+30])
                translate([rr,0]) circle(d=dd, $fn=40);
    }
}

module lightening(z, h, m=mod_) {
    translate([0,0,-1]) linear_extrude(h+2) lightening_2d(z, m);
}

// ---------------------------------------------------------------------
//  CENTRE MOUNT -- this is the hole that grips the shaft.
//  Cut it to full height h; callers subtract it inside difference().
// ---------------------------------------------------------------------
module center_mount(h, type = undef) {
    t = (type == undef) ? mount_type : type;

    if (t == "hex") {
        // $fn=6 cylinder takes the ACROSS-CORNERS diameter
        translate([0,0,-1]) cylinder(d = hex_af/cos(30), h = h+2, $fn = 6);

    } else if (t == "thread") {
        translate([0,0,-1]) cylinder(d = thread_d, h = h+2, $fn = 48);

    } else if (t == "bearing") {
        translate([0,0,-1]) cylinder(d = brg_id + 1.5, h = h+2, $fn = 64);
        // pocket opens on the TOP face; flip the part in the slicer if
        // you would rather it opened downward
        translate([0,0,h-brg_w]) cylinder(d = brg_od, h = brg_w+1, $fn = 96);

    } else if (t == "dshaft") {
        // the HOLE is the circle clipped by the flat, so the part keeps
        // material on that side -- that is what drives the D-cut shaft
        translate([0,0,-1]) intersection() {
            cylinder(d = bore, h = h+2, $fn = 64);
            translate([-bore, -bore, 0]) cube([bore + d_flat, bore*2, h+2]);
        }

    } else {
        translate([0,0,-1]) cylinder(d = bore, h = h+2, $fn = 64);
    }
}

// ---------------------------------------------------------------------
//  BOLT CIRCLE -- for bolting a camera plate, pulley or flange on.
// ---------------------------------------------------------------------
module bolt_circle_2d(enable = true) {
    if (enable && bc_holes > 0)
        for (i = [0 : bc_holes-1]) rotate([0,0, i*360/bc_holes])
            translate([bc_r, 0]) circle(d = bc_d, $fn = 32);
}

module bolt_circle(h, enable = true) {
    translate([0,0,-1]) linear_extrude(h+2) bolt_circle_2d(enable);
    if (enable && bc_cbore && bc_holes > 0)
        for (i = [0 : bc_holes-1]) rotate([0,0, i*360/bc_holes])
            translate([bc_r, 0, h-3]) cylinder(d = bc_d + 2.6, h = 4, $fn = 32);
}

// ---------------------------------------------------------------------
//  FLAT 2D PROFILE -- for laser / waterjet cutting from sheet metal.
//  Export DXF (NOT STL -- a mesh throws away the involute):
//    openscad -o wheel.dxf -D 'part="dxf"' -D 'flank_pts=60' \
//             -D 'backlash=0.35' star_tracker_gears.scad
//  Raise flank_pts hard: the default 14 is fine for a nozzle to smear
//  over, and far too coarse to hand a cutting service.
// ---------------------------------------------------------------------
module flat_2d(z = zf) {
    difference() {
        gear2d(z);
        circle(d = bore, $fn = 128);
        bolt_circle_2d(bc_on != "none");
        lightening_2d(z);
    }
}

// M3 grub screw, thread-forming into plastic
module setscrew(zpos, r = 0) {
    translate([0,0,zpos]) rotate([0,-90,0])
        cylinder(d = 2.7, h = ((r > 0) ? r : hub_d/2) + 3, $fn = 24);
}

// ---------------------------------------------------------------------
//  printable parts
// ---------------------------------------------------------------------

// compound reduction gear: big wheel with the next pinion stacked on top
module stage_gear(zw_=zw, zp_=zp) {
    H  = fw_wheel + fw_pinion;
    hd = min(hub_d, 2*g_rf(zp_) - 1);   // hub must not swallow the pinion
    // NOTE: a bearing pocket does not fit here -- brg_od (16) is wider than
    // the pinion root circle (12.5). Bearings belong in the FRAME, not in
    // the compound gear. Use round/dshaft/hex here.
    difference() {
        union() {
            gear3d(zw_, fw_wheel,  1);
            translate([0,0,fw_wheel]) gear3d(zp_, fw_pinion, -1);
            cylinder(d=hd, h=fw_wheel, $fn=48);
        }
        center_mount(H);
        bolt_circle(fw_wheel, bc_on == "all");
        lightening(zw_, fw_wheel);
        // no setscrew: stages 1-3 TURN on nails that are pressed into the
        // plate, so a grub screw here would lock the train
    }
}

// final output wheel -- the only one whose accuracy really matters
module output_wheel(z=zf, m=mod_) {
    // boss must be wide enough for whatever mount was chosen
    hd = max(hub_d + 6,
             (out_mount == "bearing")  ? brg_od + 6 : 0,
             (out_mount == "thread")   ? thread_d + 8 : 0);
    H  = fw_wheel + 4;
    difference() {
        union() {
            gear3d(z, fw_wheel, 1, m);
            cylinder(d = hd, h = H, $fn = 64);
        }
        center_mount(H, out_mount);
        bolt_circle(fw_wheel, bc_on != "none");
        lightening(z, fw_wheel, m);
        setscrew(fw_wheel + 2, hd/2);
    }
}

// motor pinion -- NEMA17 shaft
module motor_pinion(z=zp, m=mod_) {
    difference() {
        gear3d(z, fw_pinion, -1, m);
        center_mount(fw_pinion);
        setscrew(fw_pinion*0.5, g_rf(z,m));
    }
}

// ---------------------------------------------------------------------
//  bare wheel, no pinion, no hub -- the mate for the backlash test.
//  Render with -D zw=30 to get a small, fast test wheel that has exactly
//  the same tooth geometry as the real 120T.
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
//  LASER FINAL STAGE
// ---------------------------------------------------------------------

// Shaft 3, ONE piece: module-1 120T wheel below, module-2 24T pinion above.
// Single part means no concentricity joint on the stage that matters most.
module final_stage_gear() {
    H = fw_wheel + fw_pinion;
    difference() {
        union() {
            gear3d(zw,  fw_wheel,   1, mod_);
            translate([0,0,fw_wheel]) gear3d(zpf, fw_pinion, -1, mod_f);
        }
        center_mount(H);
        lightening(zw, fw_wheel);
        // no setscrew: this gear turns on a fixed nail (see stage_gear)
    }
}

// The flat profile that gets cut from sheet. No lightening holes: they would
// only add cut length (= cost) and remove stiffness from a part that is
// already light. Clamped between two printed flanges via the bolt circle.
module laser_wheel_2d() {
    difference() {
        gear2d(zwf, mod_f);
        circle(d = lw_bore, $fn = 128);
        for (i = [0 : lw_bolts-1]) rotate([0,0, i*360/lw_bolts])
            translate([lw_bc_r, 0]) circle(d = lw_bc_d, $fn = 32);
    }
}

// Printable proof copy -- run the mesh in PLA before spending money on metal.
module laser_wheel_3d(t = 1.6) {
    linear_extrude(height = t) laser_wheel_2d();
}

module test_wheel(z=zw) {
    difference() {
        gear3d(z, fw_wheel, 1);
        center_mount(fw_wheel);
        lightening(z, fw_wheel);
    }
}

// ---------------------------------------------------------------------
//  test pair -- ON SCREEN ONLY. The two gears are intermeshed here, so
//  this is NOT printable as one file. Use part="testwheel" + pinion.stl.
//  Correctly phased, and animated with $t.
//  Check: does it turn freely, and can you feel slop? Tune `backlash`.
// ---------------------------------------------------------------------
module test_pair(z1=zp, z2=zw, spin=0) {
    a = cdist(z1,z2);
    rotate([0,0, spin]) gear3d(z1, fw_pinion, -1);
    translate([a,0,0])
        rotate([0,0, 180 - 180/z2 - spin*z1/z2]) gear3d(z2, fw_wheel, 1);
}

// ---------------------------------------------------------------------
//  train layout -- centre distances and envelope only.
//  Tooth phase is NOT resolved here; use test_pair for mesh checking.
// ---------------------------------------------------------------------
cds  = [cdist(zp,zw), cdist(zp,zw), cdist(zp,zw), cdist(zpf,zwf,mod_f)];
// A square fold. Gears sharing a Z band must not collide, which sets three
// clearances:  |s0-s2| and |s1-s3| > 69.5 mm  (a 15T pinion against a 120T
// wheel), and |s2-s4| > 106.5 mm (a 15T pinion against the 96T output wheel).
// 90 deg turns give 95.5, 95.5 and 137.7 mm. The earlier [0,105,-105,0] put
// shafts 1 and 3 only 34.9 mm apart -- two 122 mm wheels straight through
// each other.
dirs = [0, 90, 180, 270];

function shaftpos(i) = (i == 0) ? [0,0]
    : shaftpos(i-1) + cds[i-1]*[cos(dirs[i-1]), sin(dirs[i-1])];

module train() {
    let(p = shaftpos(0)) translate([p[0], p[1], sp_clr]) motor_pinion();
    for (i=[1:3])
        let(p = shaftpos(i))
            translate([p[0], p[1], sp_clr + (i-1)*sp_step()])
                if (i == 3) final_stage_gear(); else stage_gear();
    let(p = shaftpos(4)) translate([p[0], p[1], sp_clr + 3*sp_step()])
        output_wheel(zwf, mod_f);

    // centre-distance callouts
    for (i=[0:3]) let(a = shaftpos(i), b = shaftpos(i+1))
        color("red") translate([(a[0]+b[0])/2, (a[1]+b[1])/2, 25])
            linear_extrude(0.5) text(str(cds[i]), size=5, halign="center");
}

// ---------------------------------------------------------------------
//  BACKLASH TEST JIG
//
//  A nail in a board has ~0.5 mm of radial play -- twice the backlash you
//  are trying to measure, so you end up measuring the mount. These posts
//  are printed to a known diameter instead.
//
//  Printed holes come out UNDERSIZE and printed posts come out OVERSIZE,
//  by an amount specific to your printer and filament. So print the fit
//  gauge FIRST, find which stub slides into your gear bore with no
//  perceptible rock, set tp_post_d to that number, then print the plate.
// ---------------------------------------------------------------------
module plate_outline(cd) {
    x0 = -(g_ra(zp) + 8);
    x1 = cd + g_ra(tp_zw) + 8;
    yy = g_ra(tp_zw) + 10;
    translate([(x0+x1)/2, 0])
        offset(r=4) square([x1-x0-8, 2*yy-8], center=true);
}

module test_plate() {
    cd = cdist(zp, tp_zw);
    th = [fw_pinion, fw_wheel];     // gear thickness at each post
    difference() {
        union() {
            linear_extrude(tp_plate_t) plate_outline(cd);
            for (i=[0:1]) translate([i*cd, 0, tp_plate_t]) {
                // collar lifts the gear clear of the plate face, so any
                // elephant's foot ends up below the running surface
                cylinder(d = tp_collar_d, h = tp_collar_h, $fn = 48);
                cylinder(d = tp_post_d,
                         h = tp_collar_h + th[i] + 1.4, $fn = 64);
            }
        }
        // M3 pilot, right through so you can drive the screw from below
        for (i=[0:1]) translate([i*cd, 0, -1])
            cylinder(d = tp_screw_d, h = tp_plate_t + tp_collar_h + th[i] + 6,
                     $fn = 24);
        translate([cd/2, -(g_ra(tp_zw)+5), tp_plate_t-0.6])
            linear_extrude(1)
                text(str("CD ", cd, "   m", mod_, "   bl", backlash),
                     size = 4, halign = "center", valign = "center");
    }
    // keeper washers -- go under an M3 screw head to stop the gear climbing
    for (i=[0:1]) translate([i*16, g_ra(tp_zw)+18, 0]) keeper();
}

module keeper() {
    difference() {
        cylinder(d = 12, h = 1.6, $fn = 48);
        translate([0,0,-1]) cylinder(d = 3.4, h = 4, $fn = 32);
    }
}

// Stub posts stepping through diameters, each labelled. Try your printed
// gear on each one; use the largest that still turns freely without rock.
module fit_gauge() {
    n = fg_n; d0 = round(fg_d0*100); st = round(fg_step*100); sp = 13;
    W = n*sp + 6;
    difference() {
        union() {
            linear_extrude(3) translate([W/2, 10]) offset(r=3)
                square([W-6, 14], center=true);
            for (i=[0:n-1]) translate([i*sp+8, 13, 3])
                cylinder(d = (d0+i*st)/100, h = 9, $fn = 64);
        }
        for (i=[0:n-1]) translate([i*sp+8, 3.6, 2.4])
            linear_extrude(1)
                text(str((d0+i*st)/100), size = 3, halign = "center");
    }
}

// Stepped HOLES, the mirror of the post gauge. The post gauge measured
// hole-shrink and post-growth COMBINED; this separates them. Push a real
// 5.00 mm shaft through (the shank of a 5 mm drill bit is an excellent
// gauge pin) and find the smallest nominal hole it turns freely in.
// That number is what `bore` must be set to for gears on a STEEL shaft.
module bore_gauge() {
    n = bg_n; d0 = round(bg_d0*100); st = round(bg_step*100); sp = 13;
    W = n*sp + 6;
    difference() {
        linear_extrude(bg_t) translate([W/2, 11]) offset(r=3)
            square([W-6, 18], center=true);
        for (i=[0:n-1]) translate([i*sp+8, 13, -1])
            cylinder(d = (d0+i*st)/100, h = bg_t+2, $fn = 64);
        for (i=[0:n-1]) translate([i*sp+8, 4.5, bg_t-0.6])
            linear_extrude(1)
                text(str((d0+i*st)/100), size = 3, halign = "center");
    }
}

// ---------------------------------------------------------------------
//  SHAFT SPACERS
//  Printed on end, so the top and bottom faces are first/last layers and
//  come out flat. The bore is chamfered both ends: it lets the spacer drop
//  onto the shaft, and keeps first-layer squish from cocking it over.
// ---------------------------------------------------------------------
function sp_step() = fw_wheel + z_gap;
function sp_h(i)   = sp_clr + (i < 2 ? 0 : (i-1) * sp_step());

module spacer(h, od = sp_od, id = bore) {
    difference() {
        cylinder(d = od, h = h, $fn = 64);
        translate([0,0,-1]) cylinder(d = id, h = h+2, $fn = 64);
        translate([0,0,-0.01])  cylinder(d1 = id+1.2, d2 = id, h = 0.6, $fn = 64);
        translate([0,0,h-0.59]) cylinder(d1 = id, d2 = id+1.2, h = 0.6, $fn = 64);
    }
}

// One of each, laid out in shaft order. Heights differ enough to tell apart
// by eye; the echo below prints them.
module spacer_set() {
    for (i = [0:4]) translate([i * (sp_od + 6), 0, 0]) spacer(sp_h(i));
}

// ---------------------------------------------------------------------
//  FULL ASSEMBLY -- baseplate, shafts, spacers and gears in their real
//  places. Colours are preview-only; STL export ignores them.
// ---------------------------------------------------------------------
function gear_top(i) =
      (i == 0) ? sp_clr + fw_pinion
    : (i == 4) ? sp_clr + 3*sp_step() + fw_wheel + 4
    :            sp_clr + (i-1)*sp_step() + fw_wheel + fw_pinion;

function shaft_r(i) = [g_ra(zp), g_ra(zw), g_ra(zw), g_ra(zw),
                       g_ra(zwf, mod_f)][i];

// 2D outline for cutting. Everything the plate does is in this one profile.
module baseplate_2d() {
    difference() {
        hull() for (i=[0:4]) let(p = shaftpos(i))
            translate([p[0], p[1]]) circle(r = bp_lobe[i], $fn = 96);

        // shaft 0 -- NEMA17 boss registers in the bore; the four bolts are
        // the 31 mm square pattern, so they sit at radius 31/sqrt(2)
        let(p = shaftpos(0)) translate([p[0], p[1]]) {
            circle(d = bp_motor_boss, $fn = 72);
            for (a = [45:90:315]) rotate([0,0,a])
                translate([bp_motor_bc/sqrt(2), 0]) circle(d = 3.4, $fn = 24);
        }
        // shafts 1-3 -- nails, driven up from underneath
        for (i=[1:3]) let(p = shaftpos(i))
            translate([p[0], p[1]]) circle(d = bp_shaft_d, $fn = 48);

        // shaft 4 -- tower spigot, its flange bolts, and the wedge interface
        let(p = shaftpos(4)) translate([p[0], p[1]]) {
            circle(d = bp_tower_d, $fn = 96);
            for (i=[0:2]) rotate([0,0, i*120 + 60])
                translate([bp_tower_bc/2, 0]) circle(d = 3.4, $fn = 24);
            for (i=[0:bp_mount_n-1]) rotate([0,0, i*360/bp_mount_n + 45])
                translate([bp_mount_bc/2, 0]) circle(d = bp_mount_d, $fn = 32);
        }
    }
}

module baseplate(t = bp_t) {
    translate([0,0,-t]) linear_extrude(t) baseplate_2d();
}

shaft_col = ["#8d8d8d", "#7fb2e5", "#3d85c6", "#1c4587", "#d97b3f"];

// explode > 0 lifts every gear off its spacer so the stack reads clearly
module assembly(explode = 0) {
    color("#c8c8c0") baseplate();

    for (i=[0:4]) let(p = shaftpos(i)) translate([p[0], p[1], 0]) {
        color("#9aa0a6")
            translate([0,0,-4]) cylinder(d=5, h=gear_top(i)+7+explode, $fn=32);
        color("#4a4a4a") spacer(sp_h(i));
    }

    let(p = shaftpos(0)) translate([p[0], p[1], sp_clr + explode])
        color(shaft_col[0]) motor_pinion();
    for (i=[1:3]) let(p = shaftpos(i))
        translate([p[0], p[1], sp_clr + (i-1)*sp_step() + explode])
            color(shaft_col[i]) { if (i == 3) final_stage_gear(); else stage_gear(); }
    let(p = shaftpos(4)) translate([p[0], p[1], sp_clr + 3*sp_step() + explode])
        color(shaft_col[4]) output_wheel(zwf, mod_f);
}

// Stepped POCKETS for a 625ZZ. Printed holes come out undersize, so a
// nominal 16.0 pocket will not take a 16.0 bearing. Push a real bearing into
// each and use the smallest that seats with firm thumb pressure -- not one
// you have to hammer, and not one that drops in.
module bearing_gauge() {
    sp = 21; W = bpg_n*sp + 6;
    difference() {
        linear_extrude(bpg_t) translate([W/2, 14]) offset(r=3)
            square([W-6, 22], center=true);
        for (i=[0:bpg_n-1]) translate([i*sp + 12, 14, bpg_t - 5.5])
            cylinder(d = bpg_d0 + i*bpg_step, h = 6, $fn = 72);
        for (i=[0:bpg_n-1]) translate([i*sp + 12, 3.6, bpg_t - 0.6])
            linear_extrude(1)
                text(str((round(bpg_d0*100) + i*round(bpg_step*100))/100),
                     size = 3, halign = "center");
    }
}

// ---------------------------------------------------------------------
//  OUTPUT SHAFT + AS5600 ENCODER
//
//  Datum: baseplate top = z 0. The wheel sits where the gear train puts it.
//  Everything below the plate is the encoder; everything above is the clamp
//  stack and the camera platform.
// ---------------------------------------------------------------------
function os_zw()  = sp_clr + 3*sp_step();          // wheel underside, 21.5
function os_ztop() = os_zw() + fw_wheel;           // wheel top face, 26.5

// Section cut is applied at the call site rather than through a children()
// wrapper: children() nested inside union() inside difference() inside if()
// does not resolve, and the cut silently does nothing.

// Printed: two bearing pockets, a flange on the plate top, a clamp disc under
// the plate, and a spigot below that the sensor bracket registers on.
// The two halves are separate solids with a bp_t gap where the plate is.
function os_zdisc() = -bp_t - os_disc_t;            // disc underside, -10.8
module output_bearing_tower() {
    zb = -21; zt = 14;
    bd = os_brg_od + os_brg_fit;
    difference() {
        union() {
            // lower half: spigot tube + clamp disc against the plate underside
            translate([0,0,zb]) cylinder(d = os_tower_od, h = -bp_t - zb, $fn = 72);
            translate([0,0,os_zdisc()]) cylinder(d = os_disc_d, h = os_disc_t, $fn = 72);
            // upper half: flange on the plate top + tube up to the bearing
            cylinder(d = os_flange_d, h = 4, $fn = 72);
            cylinder(d = os_tower_od, h = zt, $fn = 72);
        }
        // lower pocket opens at the BOTTOM face so the bearing can go in
        translate([0,0,zb-0.01]) cylinder(d = bd, h = os_brg_w, $fn = 72);
        // and a 45 deg cone out of it -- a flat pocket ceiling would be an
        // unsupported annular bridge
        translate([0,0, zb + os_brg_w - 0.01])
            cylinder(d1 = bd, d2 = os_shaft_d + 0.8,
                     h = (bd - os_shaft_d - 0.8)/2, $fn = 72);
        // upper pocket opens at the TOP face; its floor is supported
        translate([0,0, zt - os_brg_w]) cylinder(d = bd, h = os_brg_w + 1, $fn = 72);
        translate([0,0,zb-1]) cylinder(d = os_shaft_d + 0.8, h = zt-zb+2, $fn = 48);
        // 3 x M3x14 from the top: clearance through the flange, then
        // thread-forming 5.7 mm into the clamp disc. No nuts -- a nut under
        // the disc would sit where the AS5600 bracket stops.
        for (i=[0:2]) rotate([0,0, i*120 + 60]) translate([os_flange_bc/2, 0, 0]) {
            translate([0,0,-1]) cylinder(d = 3.4, h = 6, $fn = 24);
            translate([0,0,os_zdisc()-1]) cylinder(d = 2.7, h = os_disc_t + 1.01, $fn = 24);
        }
    }
}

// Each half gets its own socket; the dowel spans both sockets AND the plate.
module tower_pin_sockets() {
    for (i=[0:os_pin_n-1]) rotate([0,0, i*360/os_pin_n]) translate([os_pin_r, 0, 0]) {
        translate([0,0,-0.01]) cylinder(d = os_pin_d, h = os_pin_len, $fn = 32);
        translate([0,0,-bp_t - os_pin_len]) cylinder(d = os_pin_d, h = os_pin_len + 0.01, $fn = 32);
    }
}
function os_dowel_len() = 2*os_pin_len + bp_t - 0.4;   // 11.9

module output_tower_lower() {
    intersection() {
        difference() { output_bearing_tower(); tower_pin_sockets(); }
        translate([-100,-100,-200]) cube([200, 200, 200 - bp_t/2]);
    }
}

module output_tower_upper() {
    intersection() {
        difference() { output_bearing_tower(); tower_pin_sockets(); }
        translate([-100,-100,-bp_t/2]) cube([200, 200, 200]);
    }
}

// Both plate-facing faces want to be on the bed, so an integral male pin would
// print as an overhang on whichever half carried it. Separate dowels instead;
// 3 mm rod works as well.
module tower_split_view() {
    color("#7fb2e5") output_tower_lower();
    color("#2f6fa8") translate([0,0,26]) output_tower_upper();
    for (i=[0:os_pin_n-1]) rotate([0,0, i*360/os_pin_n])
        color("#d97b3f") translate([os_pin_r, 0, 6])
            cylinder(d = os_pin_d - 0.3, h = os_dowel_len(), $fn = 32);
}

module tower_dowels() {
    for (i=[0:os_pin_n-1]) translate([i*8, 0, 0])
        cylinder(d = os_pin_d - 0.3, h = os_dowel_len(), $fn = 32);
}

// Printed washer on the upper bearing's inner race. Inner race, washer, hub
// and shaft all turn together, so it never rubs; it just holds the hub off the
// stationary tower rim and outer race.
module hub_washer() {
    difference() {
        cylinder(d = os_washer_od, h = os_washer_t, $fn = 48);
        translate([0,0,-1]) cylinder(d = os_shaft_d + 0.2, h = os_washer_t + 2, $fn = 48);
    }
}

// Printed: clamps the shaft, carries the register the wheel locates on.
// Its body is shortened by the washer so the wheel stays at os_zw().
function os_hub_h() = os_zw() - 14 - os_washer_t;   // 6.5
module output_hub_lower() {
    difference() {
        union() {
            translate([0,0, os_zw()-os_hub_h()]) cylinder(d = os_hub_od, h = os_hub_h(), $fn = 72);
            translate([0,0, os_zw()])     cylinder(d = os_reg_d,  h = fw_wheel-0.5, $fn = 72);
        }
        translate([0,0, os_zw()-os_hub_h()-1]) cylinder(d = os_shaft_d+0.15, h = 20, $fn = 48);
        for (i=[0:bc_holes-1]) rotate([0,0, i*360/bc_holes])
            translate([bc_r, 0, os_zw()-os_hub_h()-1]) cylinder(d = 2.7, h = os_hub_h() + 2, $fn = 24);
        translate([0,0, os_zw()-4]) rotate([0,-90,0])
            cylinder(d = 2.7, h = os_hub_od/2 + 2, $fn = 24);
    }
}

// Printed: the other half of the clamp, and the camera platform.
// The nut pocket takes a full 1/4"-20 hex nut (7/16" AF x 5.56); Rev A's
// 2.2 mm pocket could not hold any 1/4"-20 nut.
os_uhub_h = 10;
module output_hub_upper() {
    difference() {
        translate([0,0, os_ztop()]) cylinder(d = os_hub_od, h = os_uhub_h, $fn = 72);
        translate([0,0, os_ztop()-1]) cylinder(d = 6.6,  h = os_uhub_h + 2, $fn = 48);  // 1/4-20
        translate([0,0, os_ztop()-1]) cylinder(d = 13.2, h = 6.9, $fn = 6);  // captive nut, 5.9 deep
        for (i=[0:bc_holes-1]) rotate([0,0, i*360/bc_holes])
            translate([bc_r, 0, os_ztop()-1]) cylinder(d = 3.4, h = os_uhub_h + 2, $fn = 24);
    }
}

// Printed: presses onto the shaft end. The 1.5 mm floor keeps the steel shaft
// from shunting flux out of the magnet.
module magnet_cap() {
    difference() {
        translate([0,0,-37]) cylinder(d = os_mag_d + 3, h = 9, $fn = 48);
        translate([0,0,-33]) cylinder(d = os_shaft_d + 0.15, h = 8, $fn = 48);
        translate([0,0,-37.01]) cylinder(d = os_mag_d + 0.15, h = os_mag_h, $fn = 48);
    }
}

// Printed: slides up over the tower spigot, so the sensor is concentric by
// construction rather than by eye, and stops against the underside of the
// tower's clamp disc, so the magnet gap is set by the parts too. Rev A had no
// stop and could slide up far enough to close the gap.
module as5600_bracket() {
    difference() {
        translate([0,0,-40]) cylinder(d = 30, h = 40 + os_zdisc(), $fn = 72);
        translate([0,0,-20])   cylinder(d = os_tower_od + os_bkt_fit, h = 20 + os_zdisc() + 1, $fn = 72);
        translate([0,0,-38.4]) cylinder(d = 24, h = 18.6, $fn = 72);
        translate([0,0,-40.1]) cylinder(d = 14, h = 1.9, $fn = 48);
        for (i=[-1,1]) translate([i*8.5, 0, -40.1]) cylinder(d = 2.7, h = 3, $fn = 24);
    }
}

// Printed: an optional washer that lies on the floor of the bracket's Ø24
// pocket, under the magnet. Plastic does not affect the field. The floor is
// 1.4 mm below the turning magnet cap's face, so:
//   ring  (bkt_washer_id > 0): any thickness up to ~2 mm, the hole clears the Ø9 cap
//   solid (bkt_washer_id = 0): must stay thinner than 1.4 mm; 0.8 leaves 0.6 running clearance
bkt_washer_t  = 2.0;    // thickness
bkt_washer_od = 23.2;   // loose in the Ø24 pocket (post-under-hole 0.5, plus 0.3 to drop in freely)
bkt_washer_id = 11;     // 0 = solid disc. 11 clears the Ø9 magnet cap by ~1 mm a side
module bracket_washer() {
    difference() {
        cylinder(d = bkt_washer_od, h = bkt_washer_t, $fn = 96);
        if (bkt_washer_id > 0) translate([0,0,-1]) cylinder(d = bkt_washer_id, h = bkt_washer_t + 2, $fn = 64);
        // 0.3 mm chamfer on the top outer edge so it drops into the pocket easily
        translate([0,0,bkt_washer_t - 0.3]) difference() {
            cylinder(d = bkt_washer_od + 1, h = 1, $fn = 96);
            cylinder(d1 = bkt_washer_od, d2 = bkt_washer_od - 2, h = 1, $fn = 96);
        }
    }
}

// Printed: GoPro base adapter disc, slipped over the upper hub's 1/4"-20 bolt
// under the camera's base mount. Infill is the slicer's job: ~15-20 %, 3 walls.
gp_disc_d  = 1.25 * 25.4;          // 31.75
gp_disc_t  = 0.35 * 25.4;          //  8.89
gp_bolt_d  = 0.24 * 25.4;          //  6.10, the 1/4"-20 as measured over the threads
gp_hole_d  = gp_bolt_d + 0.5;      //  6.60, post-under-hole rule
gp_chamfer = 0.6;                  // both outer edges, against elephant's foot
module gopro_base_adapter() {
    difference() {
        hull() {
            translate([0,0,gp_chamfer]) cylinder(d = gp_disc_d, h = gp_disc_t - 2*gp_chamfer, $fn = 120);
            cylinder(d = gp_disc_d - 2*gp_chamfer, h = gp_disc_t, $fn = 120);
        }
        translate([0,0,-1]) cylinder(d = gp_hole_d, h = gp_disc_t + 2, $fn = 64);
        // lead-ins so it starts onto the thread from either face
        translate([0,0,-0.01]) cylinder(d1 = gp_hole_d + 1.2, d2 = gp_hole_d, h = 0.6, $fn = 64);
        translate([0,0,gp_disc_t - 0.59]) cylinder(d1 = gp_hole_d, d2 = gp_hole_d + 1.2, h = 0.6, $fn = 64);
    }
}

// ---------------------------------------------------------------------
//  BENCH STAND -- a square tube that slides up over the motor until its rim
//  meets the plate's underside, and a base it plugs into. The plate's weight
//  goes down the tube walls; the motor only locates it.
//  Square, not round: a round tube clearing the motor's corners would be
//  ~66 across and hit the output tower's Ø41 disc 52.5 mm away (shaft 4).
//  Square, it clears that disc by 7.6 and the AS5600 bracket by 13.
//  Printed as drawn: tube on its bottom end, base flat.
// ---------------------------------------------------------------------
ms_motor   = 42.3;               // NEMA17 body, square
ms_in      = ms_motor + 0.5;     // 42.8 -- slides on (post-under-hole rule)
ms_wall    = 3.2;                // 8 lines of 0.4
ms_out     = ms_in + 2*ms_wall;  // 49.2
ms_r       = 3;                  // outer corner radius
ms_h       = 3 * 25.4;           // 76.2, plate underside to the base
// Notch at the top of the -X face (away from the gear train) for the connector
// tab on the motor's side; the cable runs out through it. Turn the tube to suit.
ms_slot_w  = 16.5;
ms_slot_h  = 32;                 // down from the top rim
ms_boss_d  = 8;                  // inner-corner screw bosses at the foot
ms_boss_h  = 12;
ms_boss_c  = ms_in/2 - 3;        // 18.4 -- merged into the corner walls
ms_pilot   = 2.7;                // M3 thread-forming, as the tower halves use
ms_base_d  = 170;                // Ø170 x 5: + skirt still fits the 220 bed
ms_base_t  = 5;
ms_sock_h  = 10;                 // socket collar the tube drops into
ms_sock_in = ms_out + 0.5;       // 49.7
ms_cb_d    = 6.5;                // M3 cap head counterbore, from underneath
ms_cb_h    = 3;

module rsquare(s, r, h) {        // square prism, rounded vertical edges, centred in XY
    hull() for (x=[-1,1], y=[-1,1])
        translate([x*(s/2-r), y*(s/2-r), 0]) cylinder(r = r, h = h, $fn = 32);
}

module motor_stand_tube() {
    difference() {
        union() {
            difference() {
                rsquare(ms_out, ms_r, ms_h);
                translate([0,0,-1]) rsquare(ms_in, 0.5, ms_h + 2);
            }
            for (x=[-1,1], y=[-1,1]) translate([x*ms_boss_c, y*ms_boss_c, 0])
                cylinder(d = ms_boss_d, h = ms_boss_h, $fn = 32);
        }
        for (x=[-1,1], y=[-1,1]) translate([x*ms_boss_c, y*ms_boss_c, -1])
            cylinder(d = ms_pilot, h = ms_boss_h, $fn = 24);
        // motor tab notch
        translate([-ms_out/2 - 1, -ms_slot_w/2, ms_h - ms_slot_h]) cube([ms_wall + 2, ms_slot_w, ms_slot_h + 1]);
        // 0.4 chamfer round the foot: no elephant's foot, and it starts into the socket
        translate([0,0,-0.01]) difference() {
            rsquare(ms_out + 2, ms_r, 0.41);
            hull() {
                rsquare(ms_out - 0.8, ms_r, 0.01);
                translate([0,0,0.4]) rsquare(ms_out, ms_r, 0.01);
            }
        }
    }
}

module motor_stand_base() {
    difference() {
        union() {
            hull() {                                            // 1 mm chamfer on the top edge
                cylinder(d = ms_base_d, h = ms_base_t - 1, $fn = 180);
                cylinder(d = ms_base_d - 2, h = ms_base_t, $fn = 180);
            }
            rsquare(ms_sock_in + 2*ms_wall, ms_r + 0.5, ms_base_t + ms_sock_h);
        }
        translate([0,0,ms_base_t]) rsquare(ms_sock_in, ms_r + 0.25, ms_sock_h + 1);
        for (x=[-1,1], y=[-1,1]) translate([x*ms_boss_c, y*ms_boss_c, 0]) {
            translate([0,0,-1]) cylinder(d = 3.4, h = ms_base_t + 2, $fn = 24);
            translate([0,0,-0.01]) cylinder(d = ms_cb_d, h = ms_cb_h, $fn = 32);
        }
    }
}

// ---------------------------------------------------------------------
//  CONTROLLER BOX -- houses the 150 x 100 Rev C board, lid up, glued (or
//  screwed through its two ears) to the -X face of the motor tube, so the
//  tube's tab notch opens straight into the box. Board x = 150 (J4 MOTOR,
//  TMC2209) is the tube end; board y = 0 (J1 12 V, J2 camera USB-A) is -Y.
//  Box frame: x = 0 far end .. pb_L tube end, y = 0 at the -Y wall, z = 0
//  bottom; the lid's top sits flush with the tube top = plate underside.
//
//  Lid: two M3x12 socket caps at the far end, down into corner posts. The
//  tube-end corners are under the plate, so there the lid carries two
//  "leaves" that hang into the box and M3x8 socket caps go in sideways
//  through the box walls. Every cap sits in a 3.2-deep counterbore: 0.2 below
//  flush.
//  Board: drops onto four standoffs (pins through its M3 holes) and four
//  snap hooks catch its long edges. GPS: on the lid's underside at the far
//  end, past the plate's edge, antenna facing up through the lid.
//  Outside edges: 1/8" round-over. Edges that print against the bed (box
//  bottom, lid top) get the same radius with a 45-degree foot, or the first
//  layers droop. The lid-to-box seam is left square, not rounded into a groove.
//
//  MEASURE AND SET: pb_tall (the DS3231 standing in J6 is the assumption),
//  pb_win (where the OLED is on the module), pb_gps / pb_ant.
// ---------------------------------------------------------------------
pb_brd     = [150, 100];
pb_holes   = [[6,6], [144,6], [6,94], [144,94]];
pb_wall    = 2.5;
pb_floor   = 2.0;
pb_ro      = 3.175;          // 1/8" round-over
pb_zone    = 9;              // far end, beyond the board, for the lid-screw posts
pb_gx      = 1;              // board to the tube-end wall
pb_gy      = 2.5;            // board edge to side wall: hook 0.3 gap + 1.4 arm + 0.8 swing
pb_so      = 6;              // standoff height under the board
pb_pcb     = 1.6;
pb_tall    = 28;             // above the board: tallest part measured 25.4 (2026-10-24) + 2.6
pb_lid     = 3.6;
pb_in      = [pb_zone + pb_brd[0] + pb_gx, pb_brd[1] + 2*pb_gy];   // 160 x 105
pb_L       = pb_in[0] + 2*pb_wall;                                   // 165
pb_W       = pb_in[1] + 2*pb_wall;                                   // 110
pb_bz      = pb_floor + pb_so;                                       // board underside
pb_tz      = pb_bz + pb_pcb;                                         // board top
pb_H       = pb_tz + pb_tall;                                        // rim = lid underside
function pb_b(p) = [pb_wall + pb_zone + p[0], pb_wall + pb_gy + p[1]];   // board -> box
pb_ear_h   = pb_H - pb_floor;                                        // ears run the box's height

pb_post    = 8;              // far-end lid-screw posts, square, in the corners
pb_boss    = 5;              // lid boss under each far screw (lid is too thin for a counterbore)
pb_cb_d    = 6.4;            // M3 socket-cap counterbore (head 5.5 x 3.0)
pb_cb_h    = 3.2;
pb_leaf    = [10, 6, 12];    // tube-end leaves: x, y (into the box), z (down from the lid)
pb_pad     = 2.0;            // wall thickening behind each leaf (2.5 wall + 2.0 = 3.2 counterbore
                             //   + 1.3); leaves the board 0.5 a side to drop in past it
pb_ear     = [20, 3, 0];     // ears hugging the tube's +/-Y faces: length, thickness (height = box)
pb_gus     = 12;             // gusset legs, ear to end wall, at the ears' foot and mid-height
pb_hooks   = [[40, 0], [100, 0], [52, 100], [112, 100]];   // board x, board edge
// OLED: on the USB half of the ideaspark module, measured off the 2026-10-24
// photo against the J20/J21 pin rows (~47-72 x 37-52), plus a margin
pb_win     = [45, 34, 74, 55];          // OLED viewport, board coords x0 y0 x1 y1
pb_pane    = [4, 1.0];                  // top recess for the glued-on clear panel: margin, depth
// GPS: not over the power section (25 mm parts), so over the low parts --
// antenna over the WROOM half of the ESP32 (~13 tall), module over the empty
// strip between the ESP32 and the bottom headers. Both stay 5+ mm clear.
// Contact label, recessed into the lid's top: two lines, turned 90 degrees to
// run across the lid, centred in the space between the viewport's panel recess
// and the lid's tube end. Across the lid it gets 110 mm, so the size is set by
// the email line fitting ~5 mm clear of the round-over (98 mm long at 5.9).
pb_label   = ["MikeMontana@gmail.com", "732 966-0708"];
pb_lbl_fnt = "Liberation Sans:style=Bold";
pb_lbl_sz  = 5.9;                        // cap height ~ size
pb_lbl_gap = 9.5;                        // baseline to baseline: ~15.4 mm overall
pb_lbl_rot = 90;                         // -90 reads the other way
pb_lbl_dep = 0.6;                        // 3 layers
pb_ant     = [89, 38, 26.2];             // patch antenna pocket: centre (board), inner side
pb_gps     = [40, 62, 35.5, 25.5];       // GY-NEO6MV2 board: x0 y0 (board), length, width

module pb_rbox(L, W, H, r) {   // vertical + bottom edges rounded; bottom with a 45-degree foot
    intersection() {
        hull() for (x=[r, L-r], y=[r, W-r]) translate([x, y, 0]) {
            translate([0,0,r]) sphere(r = r, $fn = 32);
            cylinder(r = r*(sqrt(2)-1), h = 0.01, $fn = 16);
            translate([0,0,r]) cylinder(r = r, h = max(H - r, 0.01), $fn = 32);
        }
        cube([L, W, H]);
    }
}

module pb_rcube(s, r = 1) {    // small rounded block (ears)
    hull() for (x=[r, s[0]-r], y=[r, s[1]-r], z=[r, s[2]-r])
        translate([x, y, z]) sphere(r = r, $fn = 12);
}

module pb_hook() {             // u = toward the board; the board edge sits at u = 1.7
    zc = pb_tz + 0.1;
    rotate([90, 0, 90]) linear_extrude(8, center = true)
        polygon([[0, 0.2], [1.4, 0.2], [1.4, zc], [2.3, zc], [2.3, zc + 0.3],   // roots 0.4 into the 0.6 relief floor
                 [1.4, zc + 2.2], [0, zc + 2.2]]);
}

module pcb_box() {
    yl = [pb_wall, pb_W - pb_wall];                 // inner faces of the side walls
    difference() {
        union() {
            difference() {
                union() {
                    pb_rbox(pb_L, pb_W, pb_H, pb_ro);
                    for (s = [-1, 1]) let(yi = pb_W/2 + s*(ms_out/2 + 0.25),
                                          yo = yi + s*pb_ear[1]) {          // ear's outer face
                        translate([pb_L - 2, s < 0 ? yo : yi, pb_floor])
                            pb_rcube([pb_ear[0] + 2, pb_ear[1], pb_ear_h]);
                        // gussets, ear to end wall, at the foot and mid-height
                        for (z = [pb_floor, pb_floor + pb_ear_h/2 - 1.5]) hull()
                            for (p = [[pb_L - 1, yo - s*1], [pb_L + pb_gus, yo - s*1], [pb_L - 1, yo + s*pb_gus]])
                                for (dz = [1, 2]) translate([p[0], p[1], z + dz]) sphere(r = 1, $fn = 12);
                    }
                }
                translate([pb_wall, pb_wall, pb_floor]) cube([pb_in[0], pb_in[1], pb_H]);
                // floor reliefs under the hooks: a longer, kinder flex arm (cut before
                // the hooks go in, or it cuts their roots off too)
                for (h = pb_hooks) let(p = pb_b([h[0], h[1]]))
                    translate([p[0] - 4.5, (h[1] == 0 ? p[1] - 1.9 : p[1] + 0.2), 0.6]) cube([9, 1.7, pb_floor]);
            }
            // far-end lid-screw posts
            for (y = [pb_wall, pb_W - pb_wall - pb_post])
                translate([pb_wall - 0.01, y, 0]) cube([pb_post, pb_post, pb_H - pb_boss - 0.3]);
            // wall pads behind the leaves, with a 45-degree underside
            for (s = [0, 1]) let(y0 = s ? pb_W - pb_wall - pb_pad : pb_wall)
                hull() {
                    translate([pb_L - pb_wall - 14, y0, pb_H - 14]) cube([14, pb_pad, 14]);
                    translate([pb_L - pb_wall - 14, s ? pb_W - pb_wall - 0.01 : pb_wall, pb_H - 14 - pb_pad])
                        cube([14, 0.01, 0.01]);
                }
            // board standoffs + locating pins
            for (h = pb_holes) translate(concat(pb_b(h), 0)) {
                cylinder(d = 7, h = pb_bz, $fn = 32);
                translate([0,0,pb_bz]) cylinder(d = 2.9, h = pb_pcb + 0.6, $fn = 24);
                translate([0,0,pb_bz + pb_pcb + 0.6]) cylinder(d1 = 2.9, d2 = 2.1, h = 0.4, $fn = 24);
            }
            // snap hooks on the long edges
            for (h = pb_hooks) let(p = pb_b([h[0], h[1]]))
                if (h[1] == 0) translate([p[0], p[1] - 1.7, 0]) pb_hook();
                else translate([p[0], p[1] + 1.7, 0]) mirror([0,1,0]) pb_hook();
        }
        // post pilots (M3 thread-forming)
        for (y = [pb_wall, pb_W - pb_wall - pb_post])
            translate([pb_wall + pb_post/2, y + pb_post/2, pb_H - pb_boss - 13]) cylinder(d = 2.7, h = 14, $fn = 24);
        // side screws into the leaves: counterbore + clearance through wall and pad
        for (s = [0, 1]) translate([pb_L - pb_wall - 7, s ? pb_W : 0, pb_H - pb_leaf[2]/2])
            rotate([s ? 90 : -90, 0, 0]) {
                translate([0,0,-1]) cylinder(d = pb_cb_d, h = pb_cb_h + 1, $fn = 32);
                translate([0,0,-1]) cylinder(d = 3.4, h = pb_wall + pb_pad + 2, $fn = 24);
            }
        // -Y wall: J1 12 V wires, J2 camera USB-A (room for the plug's moulding)
        translate([pb_b([9, 0])[0], -1, pb_tz + 0.5]) cube([15, pb_wall + 2, 11.5]);
        translate([pb_b([57.5, 0])[0] - 8.5, -1, pb_tz - 0.5]) cube([17, pb_wall + 2, 10]);
        // +Y wall: J3 camera 5 V wires; notches at the rim for the J7 encoder and J8 home cables
        translate([pb_b([95, 0])[0], pb_W - pb_wall - 1, pb_tz + 0.5]) cube([15, pb_wall + 2, 11.5]);
        for (x = [23, 60.5]) translate([pb_b([x, 0])[0] - 5, pb_W - pb_wall - 1, pb_H - 8]) cube([10, pb_wall + 2, 9]);
        // tube end: opening matching the tube's 16.5 x 32 tab notch (+0.5 a side)
        translate([pb_L - pb_wall - 1, pb_W/2 - ms_slot_w/2 - 0.5, pb_H + pb_lid - ms_slot_h])
            cube([pb_wall + 2, ms_slot_w + 1, ms_slot_h]);
        // ear holes for an optional M3 into the tube wall (drill the tube 2.5)
        translate([pb_L + pb_ear[0]/2 + 2, -50, pb_floor + pb_ear_h*0.75]) rotate([-90,0,0]) cylinder(d = 3.4, h = pb_W + 100, $fn = 24);
    }
}

module pcb_lid() {
    z0 = pb_H;                                      // lid underside
    difference() {
        union() {
            translate([0, 0, z0 + pb_lid]) mirror([0,0,1]) pb_rbox(pb_L, pb_W, pb_lid, pb_ro);
            // locating lip, along the side walls and the far end (not the tube end:
            // the tab opening and the leaves are there)
            lip_x0 = pb_wall + pb_post + 1;  lip_x1 = pb_L - pb_wall - 16;
            for (y = [pb_wall + 0.3, pb_W - pb_wall - 0.3 - 1.6])
                difference() {
                    translate([lip_x0, y, z0 - 3]) cube([lip_x1 - lip_x0, 1.6, 3.01]);
                    for (x = [23, 60.5]) translate([pb_b([x, 0])[0] - 6, y - 1, z0 - 4]) cube([12, 4, 5]);
                }
            translate([pb_wall + 0.3, pb_wall + pb_post + 1, z0 - 3]) cube([1.6, pb_in[1] - 2*pb_post - 2, 3.01]);
            // bosses over the far posts
            for (y = [pb_wall, pb_W - pb_wall - pb_post])
                translate([pb_wall + 0.2, y + 0.2, z0 - pb_boss]) cube([pb_post - 0.4, pb_post - 0.4, pb_boss + 0.01]);
            // tube-end leaves
            for (s = [0, 1]) translate([pb_L - pb_wall - 12,
                                       s ? pb_W - pb_wall - pb_pad - 0.2 - pb_leaf[1] : pb_wall + pb_pad + 0.2,
                                       z0 - pb_leaf[2]]) cube([pb_leaf[0], pb_leaf[1], pb_leaf[2] + 0.01]);
            // GPS antenna pocket: patch face-up against the lid, two snap nubs
            translate(concat(pb_b([pb_ant[0], pb_ant[1]]), z0 - 5)) difference() {
                translate([-pb_ant[2]/2 - 1.6, -pb_ant[2]/2 - 1.6, 0]) cube([pb_ant[2] + 3.2, pb_ant[2] + 3.2, 5.01]);
                translate([-pb_ant[2]/2, -pb_ant[2]/2, -1]) cube([pb_ant[2], pb_ant[2], 7]);
                translate([-2.5, pb_ant[2]/2 - 1, -1]) cube([5, 4, 7]);          // cable slot, toward the module
            }
            for (s = [-1, 1]) translate(concat(pb_b([pb_ant[0] + s*(pb_ant[2]/2), pb_ant[1]]), z0 - 4.6))
                rotate([90, 0, 0]) cylinder(d = 0.8, h = 8, center = true, $fn = 12);
            // GPS module rails: slide in from the +x end, rest on the ledges, parts up
            for (s = [0, 1]) let(yb = pb_b([0, pb_gps[1] + (s ? pb_gps[3] + 0.3 : -0.3)])[1],
                                 x0 = pb_b([pb_gps[0], 0])[0])
                translate([x0, yb, z0]) mirror([0, s ? 0 : 1, 0]) {
                    translate([0, 0, -8]) cube([pb_gps[2] + 1, 1.6, 8.01]);
                    // ledge, 1.5 under the board's edge, with a 45-degree back
                    hull() {
                        translate([0, -1.5, -8]) cube([pb_gps[2] + 1, 1.5, 1]);
                        translate([0, -0.01, -5.5]) cube([pb_gps[2] + 1, 0.01, 0.01]);
                    }
                    translate([pb_gps[2] + 0.4, -1.5, -7]) cube([0.6, 1.5, 0.6]);    // retaining bump
                }
            // far-end stop for the module
            translate([pb_b([pb_gps[0], 0])[0] - 1.6, pb_b([0, pb_gps[1]])[1], z0 - 8]) cube([1.6, pb_gps[3], 8.01]);
        }
        // far screws: counterbore 0.2 below flush, clearance through lid and boss
        for (y = [pb_wall, pb_W - pb_wall - pb_post]) translate([pb_wall + pb_post/2, y + pb_post/2, 0]) {
            translate([0,0,z0 + pb_lid - pb_cb_h]) cylinder(d = pb_cb_d, h = pb_cb_h + 1, $fn = 32);
            translate([0,0,z0 - pb_boss - 1]) cylinder(d = 3.4, h = pb_boss + pb_lid + 2, $fn = 24);
        }
        // leaf pilots, in line with the side-wall holes
        for (s = [0, 1]) translate([pb_L - pb_wall - 7, s ? pb_W - pb_wall - pb_pad - 0.2 : pb_wall + pb_pad + 0.2, z0 - pb_leaf[2]/2])
            rotate([s ? 90 : -90, 0, 0]) translate([0,0,-0.01]) cylinder(d = 2.6, h = pb_leaf[1] - 1, $fn = 24);
        // OLED viewport, and a recess in the top for the clear panel glued over it
        // (glue all round it and the window is sealed; there are no vents in the top)
        let(a = pb_b([pb_win[0], pb_win[1]]), b = pb_b([pb_win[2], pb_win[3]]), m = pb_pane[0]) {
            translate([a[0], a[1], z0 - 1]) cube([b[0] - a[0], b[1] - a[1], pb_lid + 2]);
            translate([a[0] - m, a[1] - m, z0 + pb_lid - pb_pane[1]]) cube([b[0] - a[0] + 2*m, b[1] - a[1] + 2*m, 2]);
        }
        // contact label
        let(x0 = pb_b([pb_win[2], 0])[0] + pb_pane[0])     // right edge of the panel recess
        translate([(x0 + pb_L)/2, pb_W/2, z0 + pb_lid - pb_lbl_dep]) rotate(pb_lbl_rot)
            linear_extrude(pb_lbl_dep + 1) for (i = [0 : len(pb_label) - 1])
                translate([0, (len(pb_label) - 1)/2 * pb_lbl_gap - i*pb_lbl_gap - pb_lbl_sz/2, 0])
                    text(pb_label[i], size = pb_lbl_sz, font = pb_lbl_fnt, halign = "center", valign = "baseline");
    }
}

module pcb_box_view() {          // box, lid lifted 30, a dummy board and its tall modules
    color("#c8ccd2") pcb_box();
    color("#7fb2e5", 0.9) translate([0,0,30]) pcb_lid();
    translate(concat(pb_b([0,0]), pb_bz)) {
        color("#2f7d4f") cube([pb_brd[0], pb_brd[1], pb_pcb]);
        color("#333") translate([43, 27.6, pb_pcb]) cube([48.6, 32.8, 13]);   // ESP32 + OLED
        color("#555") translate([110, 7, pb_pcb]) cube([21.6, 21.8, 20]);     // TMC2209 + heatsink
        color("#a33") translate([0, 15, pb_pcb]) cube([30, 55, 25.4]);        // power section, tallest measured
    }
}

module output_shaft_assembly() {
    if (os_cut) difference() {
        os_stack();
        translate([-200, 0, -200]) cube(400);
    } else os_stack();
}

module os_stack() {
    e = os_exp;
    color("#c8c8c0") translate([0,0,-bp_t])
        linear_extrude(bp_t) difference() {
            circle(d = 105, $fn = 96);
            circle(d = bp_tower_d, $fn = 72);
        }
    color("#9aa0a6") translate([0,0,-33]) cylinder(d = os_shaft_d, h = os_shaft_len, $fn = 48);
    for (z = [-21, 9]) color("#5a5f66") translate([0,0,z]) difference() {
        cylinder(d = os_brg_od, h = os_brg_w, $fn = 64);
        translate([0,0,-1]) cylinder(d = os_shaft_d, h = os_brg_w + 2, $fn = 48);
    }
    color("#7fb2e5")               output_bearing_tower();
    color("#e0c060") translate([0,0,14 + 0.5*e]) hub_washer();
    color("#3d85c6") translate([0,0, e])     output_hub_lower();
    color("#d97b3f") translate([0,0, os_zw() + 2*e]) output_wheel(zwf, mod_f);
    color("#2f6fa8") translate([0,0, 3*e])   output_hub_upper();
    color("#7fb2e5") translate([0,0,-1.0*e]) magnet_cap();
    color("#a48fd0") translate([0,0,-36.9 - 1.9*e])
        cylinder(d = os_mag_d, h = os_mag_h, $fn = 48);
    color("#aeb6c2") translate([0,0,-3.6*e]) as5600_bracket();
    color("#2f7d4f") translate([-10,-7.5,-41.6 - 3.6*e]) cube([20, 15, 1.6]);
    color("#222222") translate([-3,-3,-40 - 3.6*e]) cube([6, 6, 1.0]);
}

// ---------------------------------------------------------------------
//  print plate
// ---------------------------------------------------------------------
module plate() {
    g = g_ra(zw)*2 + 4;
    for (i=[0:2]) translate([i*g, 0, 0]) stage_gear();
    translate([0, -(g_ra(zw)+g_ra(zf)+4), 0]) output_wheel();
    translate([g, -(g_ra(zw)+g_ra(zf)+4), 0]) motor_pinion();
}

// ---------------------------------------------------------------------
if      (part == "train")     train();
else if (part == "plate")     plate();
else if (part == "stage")     stage_gear();
else if (part == "output")    output_wheel();
else if (part == "pinion")    motor_pinion();
else if (part == "testwheel") test_wheel();
else if (part == "dxf")       flat_2d(zf);
else if (part == "finalstage")     final_stage_gear();
else if (part == "finalpinion")    motor_pinion(zpf, mod_f);
else if (part == "laserwheel")     laser_wheel_3d();
else if (part == "lasercut")       laser_wheel_2d();
else if (part == "testplate")      test_plate();
else if (part == "fitgauge")       fit_gauge();
else if (part == "boregauge")      bore_gauge();
else if (part == "spacers")        spacer_set();
else if (part == "assembly")       assembly(explode);
else if (part == "encoder")        output_shaft_assembly();
else if (part == "tower")          output_bearing_tower();
else if (part == "hublower")       output_hub_lower();
else if (part == "hubupper")       output_hub_upper();
else if (part == "magnetcap")      magnet_cap();
else if (part == "as5600bracket")  as5600_bracket();
else if (part == "bracketwasher")  bracket_washer();
else if (part == "goproadapter")   gopro_base_adapter();
else if (part == "standtube")      motor_stand_tube();
else if (part == "standbase")      motor_stand_base();
else if (part == "stand")          { motor_stand_base(); translate([0,0,ms_base_t]) motor_stand_tube(); }
else if (part == "pcbbox")         pcb_box();
else if (part == "pcblid")         translate([0, pb_W, pb_H + pb_lid]) rotate([180,0,0]) pcb_lid();   // top on the bed
else if (part == "pcbview")        pcb_box_view();
else if (part == "bearinggauge")   bearing_gauge();
else if (part == "baseplate")      baseplate();
else if (part == "baseplatecut")   baseplate_2d();
else if (part == "towerlower")     output_tower_lower();
else if (part == "towerupper")     output_tower_upper();
else if (part == "towerdowels")    tower_dowels();
else if (part == "hubwasher")      hub_washer();
else if (part == "towersplit")     tower_split_view();
// all-printed module-2 final wheel -- 196 mm OD, fits a 220x220 bed
else if (part == "finalwheel")     output_wheel(zwf, mod_f);
else                          test_pair(zp, zw, $t*360);

echo(str("--- printed stages 1-3:  ", zw, "/", zp, " module ", mod_,
         "   ratio ", (zw/zp)*(zw/zp)*(zw/zp), " : 1"));
echo(str("--- laser final stage:   ", zwf, "/", zpf, " module ", mod_f,
         "   ratio ", zwf/zpf, " : 1"));
echo(str("TOTAL RATIO = ", (zw/zp)*(zw/zp)*(zw/zp)*(zwf/zpf), " : 1"));
echo(str("centre distances = ", cds, " mm"));
echo(str("final wheel OD = ", g_ra(zwf,mod_f)*2,
         "   final pinion OD = ", g_ra(zpf,mod_f)*2, " mm"));
echo(str("TOOTH TIP WIDTH  final wheel = ", tip_w(zwf,mod_f),
         " mm   final pinion = ", tip_w(zpf,mod_f), " mm"));
echo(str("  (old module-1 design was ", tip_w(zf,1.0), " mm -- too fine to laser)"));
echo(str("final pinion pitch radius = ", g_r(zpf,mod_f),
         " mm  (was ", g_r(zp,1.0), " mm)"));
echo(str("SPACER HEIGHTS shaft 0..4 = ", [for(i=[0:4]) sp_h(i)], " mm"));
echo(str("spacer OD = ", sp_od, " mm   clearance to neighbouring wheel = ",
         cdist(zp,zw) - g_ra(zw) - sp_od/2, " mm"));
