// =====================================================================
//  ISS MONITOR — sloped "cockpit" console case, triangle layout
//  Parametric OpenSCAD model. Units: mm.
//
//  Layout (front view):
//                 [ TOP ]                <- live position / globe
//        (enc) [ L ]   [ R ] (enc)       <- two secondary screens, one encoder each
//        o  | | |   | | |  o             <- LED, 6 toggles, LDR
//
//  Parts:
//    shell   rear box (floor, sides, back, top, front lip) — prints on its BACK
//    panel   sloped front plate — prints FACE DOWN
//    clamp   3 identical bars that hold the display modules against the panel (prints flat)
//    bezel   optional contrasting bezel rings, glued on (prints flat)
//
//  World frame (assembly): X = left→right as you face it, Y = front→back, Z = up.
//  "Slope frame": same X, Z runs up the sloped face, Y goes INTO the box, normal to the panel.
//
//  Everything marked MEASURE is a best guess — check it against the real part.
// =====================================================================

part = "assembly";   // [assembly, panel, shell, clamp, bezel, panel_print, shell_print, clamp_print, bezel_print]
show_mock = true;    // draw mock electronics in the assembly view
labels_on = true;    // engrave labels on the panel front

$fn = 72;

/* ---------------- Overall box ---------------- */
W        = 144;   // width
D        = 85;    // footprint depth (front lip to back wall)
lip      = 8;     // vertical lip under the sloped panel
tilt     = 35;    // panel tilt back from vertical (deg)
L        = 108;   // panel length measured along the slope
wall     = 2.5;   // shell wall thickness
corner_r = 3;     // rounding of the front edges (back edges stay square = flat on the bed)
cham     = 28;    // 45° chamfer on the two top corners (front view). 0 = plain box

H           = lip + L*cos(tilt);   // overall height
slope_depth = L*sin(tilt);         // how far back the top of the panel sits
CX          = W/2;                 // panel centre line
zs_ch       = (H - cham - lip)/cos(tilt);   // where the chamfer starts, up the slope
cham_n      = [-(L-zs_ch), -cham] / norm([L-zs_ch, cham]);   // inward normal of the right chamfer, in (x,z)
cham_mid    = [W-cham/2, (zs_ch+L)/2];

echo(str("Overall: ", W, " W x ", D, " D x ", round(H*10)/10, " H mm; top surface ", round((D-slope_depth)*10)/10, " deep"));

/* ---------------- Panel plate ---------------- */
t_panel   = 4.5;   // plate thickness
panel_gap = 0.2;   // clearance at the top and bottom edge
chamf     = 0.8;   // 45° chamfer on the front edge of the screen openings

// Panel screws: M3 flat-head into heat-set inserts in posts fused to the shell walls.
// [x, z, outward direction] — the block behind each post is stretched outward so it fuses with the nearest wall
post_xz   = [[6,6,[-1,-1]], [W-6,6,[1,-1]],
             [W-(cham_mid[0]+6*cham_n[0]), cham_mid[1]+6*cham_n[1], [cham_n[0],-cham_n[1]]],
             [cham_mid[0]+6*cham_n[0], cham_mid[1]+6*cham_n[1], [-cham_n[0],-cham_n[1]]],
             [CX-36,L-6,[0,1]], [CX+36,L-6,[0,1]]];
post_len  = 10;    // how far the posts reach into the box from the panel back
insert_d  = 4.0;   // hole for M3 heat-set insert (OD 4.2–4.6). MEASURE your inserts
insert_h  = 6;
screw_d   = 3.4;   // M3 clearance
csk_d     = 6.6;   // countersink diameter for an M3 flat head

/* ---------------- Round displays (Hosyond 1.28" GC9A01) ---------------- */
// [x, z, tab_angle]  tab_angle: direction the header tab points, 0 = right, 90 = up the slope
disp_dx = 25;    // lower screens: half the centre-to-centre distance
disp_zb = 45;    // lower screens: centre height
disp_zt = 78;    // top screen: centre height  (needs >= disp_zb + sqrt(40^2 - disp_dx^2) so the PCBs clear)
disp    = [[CX, disp_zt, 0], [CX-disp_dx, disp_zb, 135], [CX+disp_dx, disp_zb, 45]];   // top tab points sideways (right) to keep the panel short

disp_view_d  = 33.5;   // front opening (active area is 32.4)
disp_glass_d = 35.6;   // MEASURE: outer diameter of the round glass
disp_glass_h = 2.5;    // MEASURE: glass top above the PCB front face (drives pocket depth)
disp_pcb_d   = 38;     // MEASURE: PCB diameter (round part)
disp_pcb_t   = 1.6;    // PCB thickness
disp_lip_t   = 1.5;    // front lip left over the glass edge
pocket_d     = disp_glass_d + 0.8;
tab_w        = 22;     // MEASURE: width of the header tab
tab_len      = 16;     // MEASURE: how far the tab sticks out past the round PCB edge

// Clamp bars: [screen x, screen z, angle, reach one way, reach the other way]. Bosses sit at the two reaches.
// The two lower bars are horizontal and share the middle boss; the top bar is vertical (its tab points sideways),
// with its lower boss between the lower screens and its upper boss above the top screen.
bars         = [[CX-disp_dx, disp_zb, 0, 25, 25], [CX+disp_dx, disp_zb, 0, 25, 25], [CX, disp_zt, 90, 24, 24]];
boss_d       = 8;
boss_h       = 6.5;    // above the panel back
bar_w        = 8;
bar_t        = 3;
foot_r       = disp_pcb_d/2 - 3;   // where the bar's feet press on the PCB
clamp_squeeze= 0.2;

/* ---------------- Switch row ---------------- */
sw_z        = 11;      // row height up the slope
sw_pitch    = 13;
sw_x        = [CX-32.5, CX-19.5, CX-6.5, CX+6.5, CX+19.5, CX+32.5];
sw_labels   = ["NIGHT","UNITS","ALERT","TRACK","AUX 1","AUX 2"];
sw_hole_d   = 6.4;     // Taiss MTS-101: 6 mm bushing
sw_cb_d     = 12;      // rear counterbore so the nut can reach (nut is ~10 across flats). At 13 mm pitch, 13 would make neighbours touch
sw_panel_t  = 2.5;     // panel thickness left at the switch

/* ---------------- Rotary encoders (EC11 / KY-040), one per lower screen ---------------- */
enc_xz      = [[CX-50, 15], [CX+50, 15]];   // KY-040: M7 bushing, 8 mm thread, 12 mm D-shaft, board 26 x 19 with the encoder ~8 mm from one end
enc_hole_d  = 7.2;     // M7 bushing
enc_cb_d    = 15;      // rear counterbore
enc_panel_t = 2.5;
enc_knob_d  = 20;      // Taiss 20 x 15.5 knob (mock-up only)
enc_pcb     = [19, 26]; // KY-040 board (w across, length up the panel)
enc_pcb_off = 8;       // encoder axis distance from the board's lower end

/* ---------------- LED + light sensor ---------------- */
led_xz    = [CX-26, 93];  led_hole_d = 5.1;  led_label = "PASS";
ldr_top   = [CX+30, D-14];  ldr_hole_d = 5.2;   // LDR looks up through the shell's top plate (best for room light)

/* ---------------- Engraving ---------------- */
font      = "Liberation Sans:style=Bold";
eng_depth = 0.6;
lbl_size  = 2.4;       // switch labels (13 mm pitch — keep names to 5 letters)

/* ---------------- Shell features ---------------- */
// Panel-mount USB-C (rectangular flange, 2 screws) on the back wall
usbc_x      = CX;
usbc_z      = 18;
usbc_cut    = [14, 8];     // MEASURE: connector body cutout (w x h)
usbc_hole_sp= 21.5;        // MEASURE: screw hole spacing
usbc_hole_d = 2.6;         // pilot for the supplied self-tapping screws (2.5 for M3 tap)
usbc_pad    = [34, 16, 3]; // thicker pad inside the back wall so the screws bite

// 4 x 6 cm perfboard on the floor
pcb_size    = [60, 40];
pcb_hole_dx = 54;   pcb_hole_dy = 34;   // MEASURE: your perfboard's corner holes
pcb_pos     = [CX, 50];    // centre of the board (x, y) on the floor
peg_d       = 6;   peg_h = 5;   peg_hole_d = 2.5;

// Rubber feet recesses
foot_d      = 10;  foot_depth = 1;  foot_inset = 12;

// Bezel rings (optional, separate print)
bezel_od    = 42;  bezel_t = 1.5;

/* =====================================================================
   Helpers
   ===================================================================== */
module slope_frame() { translate([0,0,lip]) rotate([-tilt,0,0]) children(); }

// 2D side profile in (Y, Z): front-bottom, lip, slope, top, back
module profile_sharp() { polygon([[0,0],[0,lip],[slope_depth,H],[D,H],[D,0]]); }
module profile() {
    union() {
        offset(r=corner_r) offset(delta=-corner_r) profile_sharp();
        intersection() { profile_sharp(); translate([D/2,-1]) square([D, H+2]); }  // keep the back edges square
    }
}
module across(x0, len) { translate([x0,0,0]) rotate([90,0,90]) linear_extrude(len) children(); }
// the two top-corner chamfers, as oversized triangles in the front view (world X,Z), extruded front→back
module cham_cut2d() { if (cham > 0) { polygon([[W+5,H-cham-5],[W+5,H+5],[W-cham-5,H+5]]); polygon([[-5,H-cham-5],[-5,H+5],[cham+5,H+5]]); } }
module cham_cutters(grow=0) { translate([0,D+1,0]) rotate([90,0,0]) linear_extrude(D+2) offset(delta=grow) cham_cut2d(); }
module wedge_outer() { difference() { across(0, W) profile(); cham_cutters(0); } }
module cavity()      { difference() { across(wall, W-2*wall) offset(delta=-wall) profile_sharp(); cham_cutters(wall); } }
module inv_slope()   { rotate([tilt,0,0]) translate([0,0,-lip]) children(); }   // world → slope frame

module engrave(txt, size, halign="center") {
    translate([0, eng_depth, 0]) rotate([90,0,0])
        linear_extrude(eng_depth+0.01) text(txt, size=size, font=font, halign=halign, valign="center");
}
module ycyl(d, h) { rotate([-90,0,0]) cylinder(d=d, h=h); }   // cylinder along +Y

// unique boss positions from the bar list
function bar_ends(b) = [[b[0]-b[3]*cos(b[2]), b[1]-b[3]*sin(b[2])], [b[0]+b[4]*cos(b[2]), b[1]+b[4]*sin(b[2])]];
boss_xz = [for (b = bars) for (e = bar_ends(b)) e];

/* =====================================================================
   PANEL  (slope frame: X across, Y into the box, Z up the slope)
   ===================================================================== */
module panel_body() {
    difference() {
        difference() { translate([0,0,panel_gap]) cube([W, t_panel, L-2*panel_gap]); inv_slope() cham_cutters(0); }

        // --- screens ---
        for (d = disp) translate([d[0],0,d[1]]) {
            translate([0,-0.01,0]) ycyl(disp_view_d, t_panel+0.02);                  // view opening
            translate([0,-0.01,0]) rotate([-90,0,0])
                cylinder(d1=disp_view_d+2*chamf, d2=disp_view_d, h=chamf+0.01);     // front chamfer
            translate([0,disp_lip_t,0]) ycyl(pocket_d, t_panel);                    // glass pocket (from the back)
            rotate([0,-d[2],0])                                                     // tab / FPC connector relief
                translate([disp_pcb_d/2-4, disp_lip_t, -tab_w/2]) cube([tab_len+4, t_panel, tab_w]);
        }

        // --- toggles ---
        for (x = sw_x) translate([x,0,sw_z]) {
            translate([0,-0.01,0]) ycyl(sw_hole_d, t_panel+0.02);
            translate([0,sw_panel_t,0]) ycyl(sw_cb_d, t_panel);
        }

        // --- encoders ---
        for (e = enc_xz) translate([e[0],0,e[1]]) {
            translate([0,-0.01,0]) ycyl(enc_hole_d, t_panel+0.02);
            translate([0,enc_panel_t,0]) ycyl(enc_cb_d, t_panel);
        }

        // --- LED and LDR ---
        translate([led_xz[0],-0.01,led_xz[1]]) ycyl(led_hole_d, t_panel+0.02);

        // --- panel screws (countersunk from the front) ---
        for (p = post_xz) translate([p[0],0,p[1]]) {
            translate([0,-0.01,0]) ycyl(screw_d, t_panel+0.02);
            translate([0,-0.01,0]) rotate([-90,0,0]) cylinder(d1=csk_d, d2=screw_d, h=(csk_d-screw_d)/2+0.01);
        }

        // --- engraving ---
        if (labels_on) {
            for (i = [0:len(sw_x)-1]) translate([sw_x[i],0,sw_z+8.5]) engrave(sw_labels[i], lbl_size);
            translate([led_xz[0],0,led_xz[1]-5.5]) engrave(led_label, lbl_size);
        }
    }
    // --- clamp bosses on the back ---
    for (b = boss_xz) translate([b[0],t_panel-0.01,b[1]]) difference() {
        ycyl(boss_d, boss_h+0.01);
        translate([0,boss_h-insert_h,0]) ycyl(insert_d, insert_h+0.1);
    }
}

// one clamp bar, modelled flat: X along the bar (0 = screen centre), Z = thickness. hn/hp = reach to each boss.
module clamp_bar(hn=25, hp=25) {
    difference() {
        union() {
            translate([-hn-bar_w/2, -bar_w/2, 0]) cube([hn+hp+bar_w, bar_w, bar_t]);
            foot_h = boss_h - disp_pcb_t + clamp_squeeze;
            for (s = [-1,1]) translate([s*foot_r-3, -bar_w/2, -foot_h]) cube([6, bar_w, foot_h+0.01]);
        }
        for (x = [-hn, hp]) translate([x, 0, -1]) cylinder(d=screw_d, h=bar_t+2);
    }
}
module clamps_on_panel() {
    for (b = bars) translate([b[0], t_panel+boss_h, b[1]]) rotate([-90,0,0]) rotate([0,0,b[2]]) clamp_bar(b[3], b[4]);
}

module bezel_ring() {
    difference() {
        cylinder(d=bezel_od, h=bezel_t);
        translate([0,0,-1]) cylinder(d=disp_view_d+1, h=bezel_t+2);
        translate([0,0,bezel_t-0.8]) cylinder(d1=disp_view_d+1, d2=disp_view_d+1+1.6, h=0.81);
    }
}

/* =====================================================================
   SHELL (world frame)
   ===================================================================== */
module post_block(p) {
    d = p[2];
    slope_frame() hull() {
        translate([p[0]-5, t_panel, p[1]-5]) cube([10, post_len, 10]);
        translate([p[0]-5+12*d[0], t_panel, p[1]-5+12*d[1]]) cube([10, post_len, 10]);   // reaches into the wall, trimmed by the outer shape
    }
}

module shell() {
    difference() {
        union() {
            difference() { wedge_outer(); cavity(); }
            intersection() { wedge_outer(); for (p = post_xz) post_block(p); }
            translate([usbc_x-usbc_pad[0]/2, D-wall-usbc_pad[2], usbc_z-usbc_pad[1]/2]) cube([usbc_pad[0], usbc_pad[2]+0.01, usbc_pad[1]]);
            for (sx=[-1,1], sy=[-1,1]) translate([pcb_pos[0]+sx*pcb_hole_dx/2, pcb_pos[1]+sy*pcb_hole_dy/2, wall-0.01])
                hull() { cylinder(d=peg_d, h=peg_h+0.01); translate([0,peg_h,0]) cylinder(d=peg_d, h=0.01); }   // 45° gusset so it prints on its side
        }
        slope_frame() translate([-1,-1,0]) cube([W+2, t_panel+1, L]);                              // panel seat
        for (p = post_xz) slope_frame() translate([p[0], t_panel-0.01, p[1]]) ycyl(insert_d, insert_h);
        translate([usbc_x-usbc_cut[0]/2, D-wall-usbc_pad[2]-1, usbc_z-usbc_cut[1]/2]) cube([usbc_cut[0], wall+usbc_pad[2]+2, usbc_cut[1]]);
        for (s=[-1,1]) translate([usbc_x+s*usbc_hole_sp/2, D-wall-usbc_pad[2]-1, usbc_z]) ycyl(usbc_hole_d, wall+usbc_pad[2]+2);
        for (sx=[-1,1], sy=[-1,1]) translate([pcb_pos[0]+sx*pcb_hole_dx/2, pcb_pos[1]+sy*pcb_hole_dy/2, wall+0.5]) cylinder(d=peg_hole_d, h=peg_h+1);
        for (x=[foot_inset, W-foot_inset], y=[foot_inset, D-foot_inset]) translate([x,y,-0.01]) cylinder(d=foot_d, h=foot_depth+0.01);
        translate([ldr_top[0], ldr_top[1], H-wall-1]) cylinder(d=ldr_hole_d, h=wall+2);   // LDR window
    }
}

/* =====================================================================
   MOCK ELECTRONICS (assembly view only)
   ===================================================================== */
module mock() {
    slope_frame() for (d = disp) translate([d[0],0,d[1]]) {
        color("black", 0.9) translate([0, t_panel-disp_glass_h, 0]) ycyl(disp_glass_d, disp_glass_h);
        color("navy", 0.8) {
            translate([0, t_panel, 0]) ycyl(disp_pcb_d, disp_pcb_t);
            rotate([0,-d[2],0]) translate([disp_pcb_d/2-6, t_panel, -tab_w/2]) cube([tab_len+6, disp_pcb_t, tab_w]);
        }
        // straight header on the back of the tab + Dupont plugs pointing rearward
        color("dimgray", 0.8) rotate([0,-d[2],0]) translate([disp_pcb_d/2+tab_len-6, t_panel+disp_pcb_t, -9]) cube([2.6, 8.5, 18]);
        color("gray", 0.5)    rotate([0,-d[2],0]) translate([disp_pcb_d/2+tab_len-6, t_panel+disp_pcb_t+8.5, -9]) cube([2.6, 14, 18]);
    }
    slope_frame() for (x = sw_x) translate([x,0,sw_z]) color("silver", 0.9) {
        translate([0,-5,0]) ycyl(6, 5+sw_panel_t);
        translate([0,-4,0]) ycyl(10, 2.5);
        translate([0,-14,0]) rotate([15,0,0]) ycyl(3, 12);
        translate([-5, sw_panel_t, -6.5]) cube([10, 12, 13]);
    }
    slope_frame() for (e = enc_xz) translate([e[0],0,e[1]]) {
        color("silver", 0.9) translate([0,-6,0]) ycyl(7, 6+enc_panel_t);
        color("black", 0.9) translate([0,-16,0]) ycyl(enc_knob_d, 12);
        color("dimgray", 0.8) translate([-6, enc_panel_t, -6]) cube([12, 10, 12]);                       // encoder body
        color("green", 0.7) translate([-enc_pcb[0]/2, enc_panel_t+10, -enc_pcb_off]) cube([enc_pcb[0], 1.6, enc_pcb[1]]);   // KY-040 board
    }
    slope_frame() color("lime", 0.8) translate([led_xz[0],-1,led_xz[1]]) ycyl(5, 8);
    color("orange", 0.8) translate([ldr_top[0], ldr_top[1], H-wall-4]) cylinder(d=5, h=4);
    color("tan", 0.8) translate([pcb_pos[0]-pcb_size[0]/2, pcb_pos[1]-pcb_size[1]/2, wall+peg_h]) cube([pcb_size[0], pcb_size[1], 1.6]);
    color("dimgray", 0.8) translate([pcb_pos[0]-26, pcb_pos[1]-14, wall+peg_h+1.6+8]) cube([52, 28, 6]);
    color("black", 0.8) translate([usbc_x-14, D, usbc_z-6]) cube([28, 4, 12]);
}

/* =====================================================================
   ASSEMBLY / PRINT LAYOUTS
   ===================================================================== */
module assembly() {
    color("slategray") shell();
    color("gainsboro") slope_frame() panel_body();
    color("orangered") slope_frame() clamps_on_panel();
    if (show_mock) mock();
}

if (part == "assembly")    assembly();
if (part == "panel")       panel_body();
if (part == "shell")       shell();
if (part == "clamp")       clamp_bar();
if (part == "bezel")       bezel_ring();

if (part == "panel_print") translate([0,L,0]) rotate([90,0,0]) panel_body();       // face down
if (part == "shell_print") translate([0,0,D]) rotate([-90,0,0]) shell();           // back down
if (part == "clamp_print") for (i=[0:2]) translate([0, i*(bar_w+4), bar_t]) rotate([180,0,0]) clamp_bar(bars[i][3], bars[i][4]);   // flat, feet up
if (part == "bezel_print") for (i=[0:2]) translate([i*(bezel_od+4), 0, 0]) bezel_ring();
