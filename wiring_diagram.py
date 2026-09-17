#!/usr/bin/env python3
"""ISS Monitor wiring diagram (triangle-layout version: 3 screens, 6 toggles, 2 KY-040 encoders, LED, LDR, C1).
Generates WIRING_DIAGRAM.svg and .png. Pure-python SVG; edit the tables at the top and re-run."""
import cairosvg

# ---------------- DOIT ESP32 DevKit V1 (30-pin) ----------------
LEFT  = ["EN","VP 36","VN 39","D34","D35","D32","D33","D25","D26","D27","D14","D12","D13","GND","VIN"]
RIGHT = ["D23","D22","TX0","RX0","D21","D19","D18","D5","TX2 17","RX2 16","D4","D2","D15","GND","3V3"]
BX0, BX1, BY0, PITCH = 560, 760, 130, 38          # board x-range, first pin y, pin pitch
def py(i): return BY0 + i*PITCH
LI = {n.split()[0]: i for i, n in enumerate(LEFT)}
RI = {n.split()[0]: i for i, n in enumerate(RIGHT)}

# ---------------- colours ----------------
C = dict(v33="#d62728", gnd="#111111", scl="#c8a000", sda="#1f77b4", dc="#2ca02c", rst="#9467bd",
         cs1="#ff7f0e", cs2="#17becf", cs3="#8c564b", sig="#4a5a8a", enc="#0b7a5b", led="#2ca02c", ldr="#e07b00")

out = []
def line(pts, col, w=2, dash=None):
    d = " ".join(f"{'M' if i==0 else 'L'}{x},{y}" for i,(x,y) in enumerate(pts))
    out.append(f'<path d="{d}" stroke="{col}" stroke-width="{w}" fill="none" stroke-linejoin="round" stroke-linecap="round"{f" stroke-dasharray={chr(34)}{dash}{chr(34)}" if dash else ""}/>')
def dot(x, y, col): out.append(f'<circle cx="{x}" cy="{y}" r="3.2" fill="{col}"/>')
def text(x, y, s, size=11, anchor="start", weight="normal", col="#222", rot=0):
    t = f' transform="rotate({rot} {x} {y})"' if rot else ""
    out.append(f'<text x="{x}" y="{y}" font-size="{size}" font-family="Helvetica, Arial, sans-serif" text-anchor="{anchor}" font-weight="{weight}" fill="{col}"{t}>{s}</text>')
def box(x, y, w, h, title=None, fill="#ffffff", stroke="#333", rx=6):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>')
    if title: text(x+w/2, y+15, title, 12, "middle", "bold")

# ================= ESP32 board =================
box(BX0, BY0-22, BX1-BX0, len(LEFT)*PITCH+10, None, "#f4f4f8", "#333", 10)
text((BX0+BX1)/2, BY0-4, "DOIT ESP32 DevKit V1", 13, "middle", "bold")
text((BX0+BX1)/2, BY0+12, "(30-pin, USB-C)", 10, "middle", col="#666")
for i, n in enumerate(LEFT):
    line([(BX0-24, py(i)), (BX0, py(i))], "#888", 1.5); text(BX0+6, py(i)+4, n, 10, "start", col="#333")
for i, n in enumerate(RIGHT):
    line([(BX1, py(i)), (BX1+24, py(i))], "#888", 1.5); text(BX1-6, py(i)+4, n, 10, "end", col="#333")
# USB-C on the board's bottom edge
box(BX0+70, py(14)+18, 60, 18, None, "#ddd", "#666", 4); text(BX0+100, py(14)+31, "USB-C", 9, "middle", col="#333")

# ================= LEFT: controls =================
CX_R = 300          # right edge of component boxes (signal stubs leave here)
CX_L = 80           # left edge (power stubs leave here)
RAIL_V = 52; RAIL_G = 34   # 3V3 and GND rails on the far left
LANE0 = 352; LANEW = 12    # vertical lanes between components and the board
lane_i = [0]
def lane(): lane_i[0] += 1; return LANE0 + (lane_i[0]-1)*LANEW

def to_left_pin(pin, yc, col, label):
    """component stub at (CX_R, yc) -> lane -> ESP32 LEFT pin"""
    lx = lane(); yp = py(LI[pin])
    line([(CX_R, yc), (lx, yc), (lx, yp), (BX0-24, yp)], col)
    text(CX_R+3, yc-3, label, 8.5, "start", "bold", col)
def from_right_pin_around(pin, yc, col, label, top, xv):
    """ESP32 RIGHT pin -> around the board (top or bottom) -> lane -> component stub at (CX_R, yc)"""
    lx = lane(); yp = py(RI[pin]); yh = 78 if top else 745
    line([(BX1+24, yp), (xv, yp), (xv, yh), (lx, yh), (lx, yc), (CX_R, yc)], col)
    text(CX_R+3, yc-3, label, 8.5, "start", "bold", col)

# rails
line([(RAIL_G, 95), (RAIL_G, 790)], C["gnd"], 2.5); text(RAIL_G, 88, "GND", 9, "middle", "bold", C["gnd"])
line([(RAIL_V, 95), (RAIL_V, 790)], C["v33"], 2.5); text(RAIL_V+8, 88, "3V3", 9, "middle", "bold", C["v33"])

# --- Encoder L ---
def encoder(y, name, pins):   # pins: dict CLK/DT/SW -> (side, pin)
    box(CX_L, y, CX_R-CX_L, 96, name)
    text((CX_L+CX_R)/2, y+30, "KY-040 rotary encoder", 9, "middle", col="#666")
    text((CX_L+CX_R)/2, y+42, "header order on the board: GND  +  SW  DT  CLK", 8, "middle", col="#888")
    ys = {"CLK": y+56, "DT": y+70, "SW": y+84}
    for k, yy in ys.items(): text(CX_R-6, yy+3, k, 9, "end", "bold")
    # power on the left edge
    line([(CX_L, y+60), (RAIL_V, y+60)], C["v33"]); dot(RAIL_V, y+60, C["v33"]); text(CX_L+5, y+63, "+", 9, "start", "bold", C["v33"])
    line([(CX_L, y+80), (RAIL_G, y+80)], C["gnd"]); dot(RAIL_G, y+80, C["gnd"]); text(CX_L+5, y+83, "GND", 9, "start", "bold", C["gnd"])
    return ys

ysL = encoder(100, "ENCODER L  (lower-left screen)", {})
to_left_pin("VP", ysL["CLK"], C["enc"], "VP 36")
to_left_pin("VN", ysL["DT"],  C["enc"], "VN 39")
to_left_pin("D27", ysL["SW"], C["enc"], "D27")

ysR = encoder(215, "ENCODER R  (lower-right screen)", {})
to_left_pin("D35", ysR["CLK"], C["enc"], "D35")
from_right_pin_around("D4",  ysR["DT"], C["enc"], "D4 ↑", True, 780)
from_right_pin_around("D19", ysR["SW"], C["enc"], "D19 ↑", True, 788)

# --- LDR (in the shell's top plate) ---
y = 340
box(CX_L, y, CX_R-CX_L, 58, "LIGHT SENSOR  (GL5539, top plate)")
text(CX_L+8, y+34, "3V3 → [LDR] → D34 node → [100 kΩ] → GND", 9)
line([(CX_L, y+40), (RAIL_V, y+40)], C["v33"]); dot(RAIL_V, y+40, C["v33"])
line([(CX_L, y+52), (RAIL_G, y+52)], C["gnd"]); dot(RAIL_G, y+52, C["gnd"])
to_left_pin("D34", y+40, C["ldr"], "D34")

# --- Toggles ---
SW = [("SW1  NIGHT", "D32", "left"), ("SW2  UNITS", "D33", "left"), ("SW3  ALERT", "D25", "left"),
      ("SW4  TRACK", "D26", "left"), ("SW5  AUX 1", "D14", "left"), ("SW6  AUX 2", "D15", "right")]
y0 = 420
for k, (name, pin, side) in enumerate(SW):
    yy = y0 + k*44
    box(CX_L+90, yy-12, CX_R-CX_L-90, 26, None, "#fbfbfb", "#555", 4)
    text(CX_L+96, yy+5, name, 10, "start", "bold")
    # lever symbol
    line([(CX_R-30, yy+1), (CX_R-14, yy-8)], "#333", 2.5); dot(CX_R-30, yy+1, "#333"); dot(CX_R-8, yy+1, "#333")
    # GND lug
    line([(CX_L+90, yy+1), (RAIL_G, yy+1)], C["gnd"]); dot(RAIL_G, yy+1, C["gnd"])
    if side == "left": to_left_pin(pin, yy+1, C["sig"], f"{pin}")
    else: from_right_pin_around(pin, yy+1, C["sig"], f"{pin} ↓", False, 796)
text(CX_L+90, y0+6*44-10, "Toggles: one lug → GPIO, other lug → GND. No resistor (INPUT_PULLUP).", 8.5, col="#666")

# --- LED ---
y = 715
box(CX_L+90, y-12, CX_R-CX_L-90, 30, None, "#fbfbfb", "#555", 4)
text(CX_L+96, y+6, "PASS LED  (green 5 mm)", 10, "start", "bold")
text(CX_L+96, y+16, "D13 → [150 Ω] → long leg (+)   short leg (−) → GND", 8, col="#666")
line([(CX_L+90, y+1), (RAIL_G, y+1)], C["gnd"]); dot(RAIL_G, y+1, C["gnd"])
to_left_pin("D13", y+1, C["led"], "D13")

# --- C1 across the rails ---
y = 775
line([(RAIL_V, y), (RAIL_V+22, y)], C["v33"]); dot(RAIL_V, y, C["v33"])
line([(RAIL_G, y+14), (RAIL_V+22, y+14)], C["gnd"]); dot(RAIL_G, y+14, C["gnd"])
line([(RAIL_V+22, y-6), (RAIL_V+22, y+6)], "#333", 3); line([(RAIL_V+22, y+8), (RAIL_V+22, y+20)], "#333", 3)
text(RAIL_V+30, y+10, "C1  100–470 µF   + to 3V3, stripe (−) to GND. Mount close to the ESP32's 3V3/GND pins.", 8.5)

# rails to the ESP32: GND from the LEFT GND pin, 3V3 from the RIGHT 3V3 pin routed under the board
yg = py(LI["GND"]); line([(BX0-24, yg), (RAIL_G, yg)], C["gnd"], 2.5); dot(RAIL_G, yg, C["gnd"])
y3 = py(RI["3V3"]); line([(BX1+24, y3), (804, y3), (804, 760), (RAIL_V, 760)], C["v33"], 2.5); dot(RAIL_V, 760, C["v33"]); dot(804, y3, C["v33"])

# ================= RIGHT: displays =================
DX0 = 1010; DW = 250
LANES = {"v33": 826, "gnd": 842, "scl": 858, "sda": 874, "dc": 890, "rst": 906, "cs1": 930, "cs2": 946, "cs3": 962}
DISP = [("TOP screen  (globe / live position)", "cs1", "D21", 110),
        ("LOWER-LEFT screen", "cs2", "D5", 300),
        ("LOWER-RIGHT screen", "cs3", "D22", 490)]
PINS = ["VCC", "GND", "SCL", "SDA", "DC", "RST", "CS"]
PINCOL = {"VCC": "v33", "GND": "gnd", "SCL": "scl", "SDA": "sda", "DC": "dc", "RST": "rst"}
for name, cs, cspin, y in DISP:
    box(DX0, y, DW, 150, name)
    text(DX0+DW/2, y+30, "Hosyond 1.28\" GC9A01, 7-pin", 9, "middle", col="#666")
    for k, p in enumerate(PINS):
        yy = y + 46 + k*16
        col = C[PINCOL.get(p, cs)]
        text(DX0+8, yy+3, p, 9, "start", "bold")
        lx = LANES[PINCOL.get(p, cs)]
        line([(DX0, yy), (lx, yy)], col); dot(lx, yy, col)
    text(DX0+DW-6, y+46+6*16+3, f"CS → {cspin}", 9, "end", col=C[cs])
# lane verticals
ymin = 110+46; ymax = 490+46+6*16
for k, x in LANES.items():
    line([(x, ymin if k not in ("cs2","cs3") else (300+46 if k=="cs2" else 490+46)), (x, ymax if k not in ("cs1","cs2") else (110+46+96 if k=="cs1" else 300+46+96))], C[k], 2.5 if k in ("v33","gnd") else 2)
# ESP32 right pins -> lanes
def right_to_lane(pin, lanekey, label):
    yp = py(RI[pin]); x = LANES[lanekey]
    line([(BX1+24, yp), (x, yp)], C[lanekey]); dot(x, yp, C[lanekey])
    text(BX1+30, yp-4, label, 8.5, "start", col=C[lanekey])
right_to_lane("D18", "scl", "SCL (clock)")
right_to_lane("D23", "sda", "SDA (data)")
right_to_lane("TX2", "dc",  "DC")
right_to_lane("RX2", "rst", "RST")
right_to_lane("D21", "cs1", "CS top")
right_to_lane("D5",  "cs2", "CS lower-left")
right_to_lane("D22", "cs3", "CS lower-right")
right_to_lane("GND", "gnd", "GND")
line([(804, y3), (LANES["v33"], y3)], C["v33"], 2.5); dot(LANES["v33"], y3, C["v33"]); text(BX1+30, y3-4, "3V3 (displays run on 3.3 V, not VIN)", 8.5, col=C["v33"])
# extend v33/gnd lanes down to the ESP32 pins
line([(LANES["v33"], ymax), (LANES["v33"], y3)], C["v33"], 2.5)
line([(LANES["gnd"], ymax), (LANES["gnd"], py(RI["GND"]))], C["gnd"], 2.5)
text(DX0, 680, "All three displays share 3V3, GND, SCL, SDA, DC and RST.", 9.5, col="#444")
text(DX0, 694, "Only CS is separate — that is how the code picks a screen.", 9.5, col="#444")
text(DX0, 712, "Optional: 100 nF ceramic across VCC/GND at each display.", 9, col="#666")
text(DX0, 730, "⚠ Replace the in-plane right-angle header with a straight", 9, col="#b00")
text(DX0, 742, "header on the back, or solder wires — see CASE_NOTES.", 9, col="#b00")

# ================= USB-C + notes =================
box(BX0-40, 800, 280, 46, None, "#f4f4f8", "#333", 8)
text(BX0+100, 818, "Panel-mount USB-C (back wall)", 11, "middle", "bold")
text(BX0+100, 834, "0.3 m cable inside the case → ESP32 USB-C port. Powers everything.", 9, "middle", col="#555")
line([(BX0+100, py(14)+36), (BX0+100, 800)], "#666", 2, "4,4")

box(20, 810, 470, 70, None, "#fffbe6", "#c9a400", 8)
text(30, 828, "Notes", 11, "start", "bold")
text(30, 843, "• KY-040 '+' goes to 3V3, never 5 V. Its board has pull-ups on CLK/DT; SW uses INPUT_PULLUP.", 9)
text(30, 856, "• GPIO 34/35/36/39 are input-only (fine for LDR and encoder CLK/DT). D15 is a strapping pin — harmless for a toggle.", 9)
text(30, 869, "• ↑ / ↓ = wire routed over / under the board from a right-side pin. Every safe GPIO is now used. Match wires by the printed label.", 9)

# legend
lg = [("3V3", "v33"), ("GND", "gnd"), ("SCL", "scl"), ("SDA", "sda"), ("DC", "dc"), ("RST", "rst"), ("CS top", "cs1"), ("CS LL", "cs2"), ("CS LR", "cs3"), ("toggles", "sig"), ("encoders", "enc")]
for k, (n, key) in enumerate(lg):
    x = 20 + k*72; line([(x, 40), (x+22, 40)], C[key], 3); text(x+27, 44, n, 9)
text(20, 22, "ISS MONITOR — wiring (triangle console: 3 screens, 6 toggles, 2 encoders, LED, LDR)", 15, "start", "bold")
text(1320, 22, "junction dots = connected; crossings without a dot are not", 9, "end", col="#666")

svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="1340" height="900" viewBox="0 0 1340 900"><rect width="100%" height="100%" fill="white"/>' + "\n".join(out) + "</svg>"
open("/mnt/user-data/outputs/WIRING_DIAGRAM.svg", "w").write(svg)
cairosvg.svg2png(bytestring=svg.encode(), write_to="/mnt/user-data/outputs/WIRING_DIAGRAM.png", output_width=2440)
print("ok")
