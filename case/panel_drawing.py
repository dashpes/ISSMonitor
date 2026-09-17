#!/usr/bin/env python3
"""2D front-panel layout drawing for the ISS Monitor console.
Numbers mirror the top of ISS_console.scad — edit both together (or just edit here to try spacing ideas).
Outputs: panel_layout.png (annotated) and panel_layout_1to1.pdf (true scale, print at 100% on Letter)."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Rectangle, Polygon
import math, sys

# ---------------- parameters (slope frame: x across, z up the panel) ----------------
W, L = 144, 108
CX = W/2
disp_dx, disp_zb, disp_zt = 25, 45, 78
disp = [(CX, disp_zt, 0), (CX-disp_dx, disp_zb, 135), (CX+disp_dx, disp_zb, 45)]   # top tab points sideways → shorter panel   # x, z, tab angle
view_d, glass_d, pocket_d, pcb_d = 33.5, 35.6, 36.4, 38
tab_w, tab_len = 22, 16
bars = [(CX-disp_dx, disp_zb, 0, 25, 25), (CX+disp_dx, disp_zb, 0, 25, 25), (CX, disp_zt, 90, 24, 24)]   # x, z, angle, reach-, reach+
boss_d = 8
sw_z, sw_x = 11, [CX-32.5, CX-19.5, CX-6.5, CX+6.5, CX+19.5, CX+32.5]   # 13 pitch
sw_labels = ["NIGHT","UNITS","ALERT","TRACK","AUX 1","AUX 2"]
sw_hole, sw_cb = 6.4, 12
enc = [(CX-50, 15), (CX+50, 15)]
enc_hole, enc_cb, knob_d = 7.2, 15, 20
led = (CX-26, 93)   # LDR moves to the shell top plate
lip, tilt = 8, 35
H = lip + L*math.cos(math.radians(tilt))
cham = 28                                   # 45° chamfer legs in the front view (world)
zs_ch = (H - cham - lip)/math.cos(math.radians(tilt))   # where the chamfer starts, up the slope
outline = [(0,0),(W,0),(W,zs_ch),(W-cham,L),(cham,L),(0,zs_ch)]
# corner posts sit 6 mm inside the chamfer's midpoint
_d = (-cham, L-zs_ch); _n = math.hypot(*_d); _nx, _nz = -_d[1]/_n, _d[0]/_n   # inward normal (right side)
_mx, _mz = W-cham/2, (zs_ch+L)/2
posts = [(6,6),(W-6,6),(W-_mx+6*_nx*-1, _mz+6*_nz) if False else (W-(_mx+6*_nx), _mz+6*_nz),(_mx+6*_nx, _mz+6*_nz),(CX-36,L-6),(CX+36,L-6)]
ldr = None

def bosses():
    pts = []
    for x, z, a, hn, hp in bars:
        for r in (-hn, hp):
            p = (round(x + r*math.cos(math.radians(a)), 3), round(z + r*math.sin(math.radians(a)), 3))
            if p not in pts: pts.append(p)
    return pts

def tab_poly(x, z, a):
    ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
    r0, r1, hw = pcb_d/2 - 4, pcb_d/2 + tab_len, tab_w/2
    pts = [(r0, -hw), (r1, -hw), (r1, hw), (r0, hw)]
    return [(x + u*ca - v*sa, z + u*sa + v*ca) for u, v in pts]

def draw(ax, annotate=True):
    ax.add_patch(Polygon(outline, fill=False, lw=1.2, color="k"))
    for x, z, a in disp:
        ax.add_patch(Circle((x, z), view_d/2, fill=False, lw=1.0, color="k"))
        ax.add_patch(Circle((x, z), pocket_d/2, fill=False, lw=0.6, ls="--", color="tab:blue"))
        ax.add_patch(Circle((x, z), pcb_d/2, fill=False, lw=0.6, ls=":", color="tab:blue"))
        ax.add_patch(Polygon(tab_poly(x, z, a), fill=False, lw=0.6, ls=":", color="tab:blue"))
        ax.plot([x-2, x+2], [z, z], "k-", lw=0.4); ax.plot([x, x], [z-2, z+2], "k-", lw=0.4)
    for bx, bz in bosses():
        ax.add_patch(Circle((bx, bz), boss_d/2, fill=False, lw=0.6, color="tab:red"))
        ax.add_patch(Circle((bx, bz), 2, fill=False, lw=0.4, color="tab:red"))
    for x, z, a, hn, hp in bars:
        ax.add_patch(Rectangle((x-hn-4, z-4), hn+hp+8, 8, angle=a, rotation_point=(x, z), fill=False, lw=0.4, ls="--", color="tab:red"))
    for x in sw_x:
        ax.add_patch(Circle((x, sw_z), sw_hole/2, fill=False, lw=1.0, color="k"))
        ax.add_patch(Circle((x, sw_z), sw_cb/2, fill=False, lw=0.5, ls="--", color="tab:green"))
    for x, z in enc:
        ax.add_patch(Circle((x, z), enc_hole/2, fill=False, lw=1.0, color="k"))
        ax.add_patch(Circle((x, z), enc_cb/2, fill=False, lw=0.5, ls="--", color="tab:green"))
        ax.add_patch(Circle((x, z), knob_d/2, fill=False, lw=0.5, ls=":", color="tab:purple"))
        ax.add_patch(Rectangle((x-9.5, z-8), 19, 26, fill=False, lw=0.5, ls=":", color="tab:green"))   # KY-040 board, 10 mm behind panel
    ax.add_patch(Circle(led, 5.1/2, fill=False, lw=1.0, color="k"))
    for x, z in posts:
        ax.add_patch(Circle((x, z), 3.4/2, fill=False, lw=0.8, color="k"))
        ax.add_patch(Circle((x, z), 6.6/2, fill=False, lw=0.4, color="k"))
    if annotate:
        for x, lbl in zip(sw_x, sw_labels):
            ax.text(x, sw_z+8.5, lbl, ha="center", va="center", fontsize=5, family="DejaVu Sans", weight="bold")
        ax.text(led[0], led[1]-5.5, "PASS", ha="center", va="center", fontsize=5, weight="bold")

def dim_h(ax, x0, x1, y, txt, off=0):
    ax.annotate("", (x0, y), (x1, y), arrowprops=dict(arrowstyle="<->", lw=0.6, color="tab:gray"))
    ax.text((x0+x1)/2, y+1.2+off, txt, ha="center", va="bottom", fontsize=6, color="dimgray")
def dim_v(ax, x, y0, y1, txt, side=1):
    ax.annotate("", (x, y0), (x, y1), arrowprops=dict(arrowstyle="<->", lw=0.6, color="tab:gray"))
    ax.text(x+1.2*side, (y0+y1)/2, txt, ha="left" if side > 0 else "right", va="center", fontsize=6, color="dimgray", rotation=90)

# ---------------- annotated PNG ----------------
fig, ax = plt.subplots(figsize=(13, 9.5))
draw(ax)
dim_h(ax, 0, W, L+8, f"{W}")
dim_h(ax, cham, W-cham, L+3, f"{W-2*cham} top edge")
dim_v(ax, W+8, 0, zs_ch, f"{zs_ch:.1f} to chamfer", 1)
dim_v(ax, W+16, 0, L, f"{L}", 1)
dim_h(ax, CX-disp_dx, CX+disp_dx, -7, f"{2*disp_dx} (lower screens c-c)")
dim_v(ax, CX+disp_dx+24, disp_zb, disp_zt, f"{disp_zt-disp_zb}", 1)
dim_v(ax, -8, 0, sw_z, f"{sw_z}", -1)
dim_v(ax, -16, 0, disp_zb, f"{disp_zb}", -1)
dim_v(ax, -24, 0, disp_zt, f"{disp_zt}", -1)
dim_h(ax, sw_x[0], sw_x[1], sw_z-6, "13 pitch")
dim_h(ax, sw_x[2], sw_x[3], sw_z-6, "13")
dim_h(ax, sw_x[-1], enc[1][0], sw_z-6, f"{enc[1][0]-sw_x[-1]:.1f}")
dim_h(ax, CX, enc[1][0], -12.5, f"{enc[1][0]-CX:.0f} centre → encoder")
dim_v(ax, W+24, 0, enc[1][1], f"{enc[1][1]} enc", 1)
dim_v(ax, CX+8, disp_zt-24, disp_zt+24, "48 (top bar)", 1)
ax.text(2, -17, "Solid = through hole / opening.  Blue dashed = glass pocket (back), blue dotted = PCB outline + header tab.  "
        "Red = clamp bosses & bars (back).  Green dashed = nut counterbore (back).  Purple dotted = 20 mm knob, green dotted = KY-040 board (10 mm behind the panel).",
        fontsize=6.5, color="dimgray")
ax.text(2, -21, "Holes: screens Ø33.5 view / Ø36.4 pocket · toggles Ø6.4 (Ø12 cb) · encoders Ø7.2 (Ø15 cb) · LED Ø5.1 · LDR Ø5.2 · panel screws Ø3.4 csk Ø6.6 · bosses Ø8 with M3 insert",
        fontsize=6.5, color="dimgray")
ax.set_xlim(-30, W+30); ax.set_ylim(-24, L+14); ax.set_aspect("equal"); ax.axis("off")
ax.set_title(f"ISS Monitor front panel (slope frame, mm) — {W} × {L}, 4.5 thick, top corners chamfered {cham} (45° in front view)", fontsize=9)
fig.tight_layout(); fig.savefig("/mnt/user-data/outputs/case/panel_layout.png", dpi=170)

# ---------------- 1:1 PDF (Letter landscape, 11 x 8.5 in) ----------------
fig2 = plt.figure(figsize=(11, 8.5))
ax2 = fig2.add_axes([0, 0, 1, 1])
mm = 1/25.4
ox, oz = (11 - W*mm)/2/mm, (8.5 - L*mm)/2/mm     # centre the panel on the page, in mm
ax2.set_xlim(-ox, W+ox); ax2.set_ylim(-oz, L+oz); ax2.set_aspect("equal"); ax2.axis("off")
draw(ax2)
ax2.text(0, L+4, "ISS Monitor front panel — 1:1. Print at 100% / 'Actual size'. Check: this bar is 100 mm →", fontsize=7)
ax2.plot([W-100, W], [L+5, L+5], "k-", lw=1.5)
fig2.savefig("/mnt/user-data/outputs/case/panel_layout_1to1.pdf")
print("wrote panel_layout.png and panel_layout_1to1.pdf; bosses at", bosses())
