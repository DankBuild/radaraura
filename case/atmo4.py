#!/usr/bin/env python3
# Copyright (c) 2026 DankBuild - RadarAura (https://github.com/DankBuild/radaraura)
# Licensed under the RadarAura Build Licence 1.0 - see LICENSE.md
"""
RadarAura case generator: front body, rear cover, USB-C insert and the name inlay.

DESIGN:
  * Board (Guition JC4880P443C, 66.8 x 114.4 mm) sits behind the front window, held by
    4 M2 screws through the rear cover into the board's standoffs.
  * SEN66 (55.2 x 25.6 x 21.3 mm) sits in a chamber BELOW the screen (the "chin"), its
    ported face flush against the front wall: each air port has its own opening.
  * USB-C: both ports point straight back -> a snap-in insert in the rear cover.
  * Closed top and bottom; only the back is open, closed by the rear cover (snaps + screws).

  --------------- EDIT THESE (the physical knobs) -----------------------------
  Everything you'd fine-tune is in the PARAMS block. Change a number, re-run:
      ./.venv/bin/python atmo4.py
  ----------------------------------------------------------------------------
Coords: X=width, Z=height (top edge=IO edge), Y=depth (front face=0, back tapers in).
"""
import trimesh, numpy as np, math
from shapely.geometry import box as sbox, Polygon
from geom import box, cyl, union, diff

# ===================== PARAMS (edit me) =====================
PCB_X, PCB_Y = 66.80, 114.40   # OFFICIAL product dimensions (Guition datasheet) = the whole board. (Was 69.41x117.01 -> a phantom size that inflated the cavity AND pushed the USB/screws ~1.3mm too high.)
BOARD_RIB_CLR = 0.15         # board-edge clearance at the locating ribs. Tune: 0.10 snugger / 0.25 looser.
GLASS_X, GLASS_Y = 66.80, 114.40
ACT_X, ACT_Y = 56.16, 93.60
# --- SEN66 real dims + port layout (Sensirion drawing). Ported face = 55.2 x 25.6.
SEN_LEN, SEN_WID, SEN_HGT = 55.2, 25.6, 21.3        # length x width x height
SEN_INLET_X = 6.7          # intake (square) centre, measured from the sensor's LEFT end
SEN_FAN_X   = 42.4         # Ø20.5 fan/exhaust centre, from the LEFT end
SEN_FAN_D   = 20.5
# orientation in the case: ported face -> BACK. length->X, height(21.3)->depth Y, width(25.6)->height Z
SEN_DX, SEN_DY, SEN_DZ = SEN_LEN, SEN_HGT, SEN_WID   # 55.2(X) x 21.3(Y deep) x 25.6(Z tall)
# Option A: sensor's PORTED (air) face points FORWARD (toward the screen/room); connector faces BACK.
SEN_GAP_FRONT = 0.0        # ported face FLUSH on the inner front wall: each port mates 1:1 with its case opening
                           # (Sensirion design-in: duct the ports through the housing). Kills the forward slide AND
                           # the exhaust->inlet recirculation the old 5mm plenum+baffle needed foam to stop.
SEN_RIB_CLR   = 0.25       # side-rib clearance per side (was 0.6 -> the sensor slid sideways)
SEN_CONN_X0, SEN_CONN_X1 = -8.6, 2.3  # connector zone on the BACK face, top edge (drawing x 19..29.9 from the left end)
SEN_PUSH_X    = 23.0       # cover spring pushers at +/-23: on the flat label / fan-housing areas, clear of connector+cable
# END-CLIPS into the SEN6x mounting recesses (drawing: ~4.5mm squares on the END faces, 14.7..19.2mm behind
# the ported face). A tongue in each cradle rib carries a 45deg-ramped nub -> the sensor CLICKS in and is
# retained even with the cover off. Nub only 0.8 proud: if the recess reading is off, it degrades to a light
# friction fit on the flat end face (0.55mm squeeze) -- never a jam.
SEN_CLIP_YC   = 3.0 + (14.7+19.2)/2   # nub centre depth = recess centre (16.95 behind the ported face at y=3)
SEN_CLIP_NUB  = 3.5        # nub square side (recess ~4.5 -> ±0.5 find-tolerance)
SEN_CLIP_PROUD = 0.8       # nub projection past the rib face (bite ~0.55 into the recess)
SEN_SPRING_PRE = 0.7       # spring tip lands this far PAST the sensor's back plane -> closing the cover bends the
                           # blade = continuous forward preload. +/-0.3mm print error just varies the force a bit
                           # (no foam pad, nothing to buy, can't rattle). ~0.6% PETG strain at 0.7mm: very safe.

# --- case shell
WALL, FRONT_T, COVER_T, R = 2.4, 3.0, 2.4, 0.1   # R=0.1 -> effectively SQUARE corners (board/screen are square)   # R=0.8 -> SQUARE box (was 5.0: rounded corners ate the flat front so the 69.4mm board overhung the curve & couldn't sit flat)
COVER_Z_GAP = 0.2     # end relief: cover seats BETWEEN the body's solid top/bottom caps, not over them
W = 72.4      # = board 66.80 + 2*WALL(2.4) + 0.4/side insertion clearance (was 75.0, sized for the phantom 69.41 board -> 1.7mm/side slop)
# UNIFORM FLAT BACK (all zones equal) -> NOTHING floats, so the cover prints support-free with a clean,
# unscarred back. (Was stepped 38/20/28; a stepped back forces supports that scar the visible back.)
# Trade-off: the USB port ends up recessed ~22mm (deep) and the middle is chunkier/boxier. Same footprint.
D_CHIN, D_MID, D_PIN = 42.0, 42.0, 42.0   # was 38: +4 so the user's M2 screws (bottoming in the board's
                                          # standoffs ~4mm proud) sink into the recess
D_TOP, D_BOT = D_PIN, D_CHIN  # back-compat aliases
SEG = 40
# --- chin chamber (sensor sits ON the bottom cap, ported face FLUSH against the front wall)
SEN_Z0 = WALL                             # sensor RESTS on the bottom cap (was +0.6 float = vertical rattle; the
                                          # port holes all derive from SEN_Z0 so they track automatically)
SEN_ZC = SEN_Z0 + SEN_DZ/2
CHIN_TOP = SEN_Z0 + SEN_DZ + 1.5          # chamber ceiling (shelf) just above the sensor
SHELF_Z = CHIN_TOP
# --- board above the chamber
BOARD_BOT = SHELF_Z + 2.0
BOARD_TOP = BOARD_BOT + PCB_Y
BOARD_TOP_CLR = 0.4           # air above the glass top so the board can actually seat (was 0.0 -> ceiling pressed on the glass)
H = BOARD_TOP + BOARD_TOP_CLR + WALL          # top cap sits just above the board (was +9 -> needlessly tall, overhung the screen)
Z_CHIN = SHELF_Z              # chin (deep) below this
Z_PIN  = BOARD_TOP - 15.0     # pin bump (deep) above this; slim middle (USB) in between
GLASS_ZC = (BOARD_BOT+BOARD_TOP)/2                 # glass centred on the PCB
GLASS_TOP_Z = GLASS_ZC + GLASS_Y/2
ACT_TOP_BORDER = 9.0                               # user-measured: glass-top -> active-top (camera lives in this border)
ACT_ZC = GLASS_TOP_Z - ACT_TOP_BORDER - ACT_Y/2   # TRUE vertical centre of the lit area
WIN_ZC = ACT_ZC - 2.0                              # window ON the lit rect; -2.0 from two fit tests (-1.0 still left ~1mm black strip at the top)
WIN_REVEAL = 0.75                                  # border revealed around the lit area, per side
WIN_X, WIN_Z = ACT_X + 2*WIN_REVEAL, ACT_Y + 2*WIN_REVEAL  # window a hair LARGER than the lit area -> ALL pixels exposed; bezel covers only border + the top camera
# --- 4 board screws (your numbers)
SC_X = 27.5   # 4 board mounting holes at X = +/-27.5 from the centreline
SC = [(sx,sz) for sx in (-SC_X,SC_X) for sz in (GLASS_TOP_Z-97.8, GLASS_TOP_Z-35.7)]  # board_x screws -40.6/+21.5 -> case Z 50.0 / 112.1
STANDOFF_Y = FRONT_T + 8.5      # board's threaded-standoff plane (your "8.5mm from the screen"); boss reaches here
SCREW_CLEAR_R = 1.3             # M2 screw (user measured 2mm) -> Ø2.6 clearance bore (prints ~Ø2.3; Ø2.3 printed tight and could bind) (screw threads into the board)
SCREW_HEAD_R  = 2.3             # counterbore Ø4.6 for an M2 pan/cheese head (Ø3.8-4.0)
SCREW_HEAD_D  = 1.8             # counterbore depth; a 45deg cone below it is the seat (prints back-down, no support).
                                # An Ø4 head lands ~2.1 deep -> a 1.6mm head sits ~0.5mm below the surface.
                                # A deeper recess does NOT help a screw that bottoms out in the standoff (its
                                # head stays put); that needs a shorter screw.
# --- COVER LOCATING SKIRT (no glue; REPLACES the failed press-in lip). The rear cover is now a shallow
#     LID, not a flat plate: a perimeter skirt runs FORWARD from its back panel into the body cavity and
#     slides along the body's inner SIDE walls. That registers the lid (kills the side-wobble that let the
#     flat plate "slide through") and closes the seam, so the two halves SIT TOGETHER. The 4 board screws
#     are the real Y-clamp; the skirt is pure location. Board-safe: the skirt lives at the case BACK
#     (chin y31-38, mid y13-20, pin y21-28), far clear of the board (y<4.6), the sensor and the USB.
SKIRT_T   = 2.0      # skirt wall thickness
SKIRT_LAP = 6.0      # how far the skirt reaches forward into the cavity (long overlap -> solid, wobble-free)
# --- SNAP HOOKS (the REAL click/lock) — placed BELOW THE SCREEN in the CHIN, the only board-free space.
#     The board fills the cavity and has components right at its side edges (~10-13mm deep), so a snap
#     alongside the board would hit them. The chin (below the board) is empty at the sides (the sensor is
#     central), so the fingers live there. Each is a cantilever off the back panel reaching forward into the
#     deep chin, with a hooked tip that springs into a BLIND POCKET in the side wall (NOT a through-hole ->
#     the sensor chamber stays sealed + no holes on the case exterior). Click/lock with no screws; the 4
#     metal screws into the board standoffs are the primary hold. Bending strain ~3.7% (PETG-safe).
#     TOP snaps added in the PIN zone: it's 28mm deep (vs the slim 20mm middle) to clear the header, and the
#     header is central -> the top side-edges have room BEHIND the edge components for a finger (tip y16.2 >
#     the ~13mm components). So the cover is now locked at BOTH ends: 2 in the chin + 1 up top, per side.
SNAP_FW   = 9.0      # finger width (along Z)
SNAP_ZS   = [11.0,                                  # low chin snap
             SHELF_Z-1.25-SNAP_FW/2-0.4,            # upper chin snap: finger top 0.4 below the shelf underside (shelf is 2.5 thick, centred on SHELF_Z; hardcoded 24.0 clipped the shelf when the stack moved down)
             H-WALL-COVER_Z_GAP-SNAP_FW/2-0.4]      # top snap: derived from H (hardcoded 143.0 overran the cap after the board resize)
SNAP_FREE = 9.0      # free (bending) finger length forward of the back-panel root
SNAP_T    = 2.0      # finger thickness
SNAP_HO   = 1.2      # hook overhang past the finger face -> ~1.0mm catch into the blind pocket
# --- USB: TWO snug per-port openings in the REAR COVER. Both Type-C ports face the back; in BOARD coords
# they sit at x=37.85 (long axis), y=+8.35 / y=-2.90 (width). Board->case is a proper rotation (NO mirror):
#   case X = board y    |    case Z = GLASS_TOP_Z - 57.2 + board_x   (board_x=+57.2 = the glass top edge)
USB_L_CX = -2.90                            # -X port centre  (board y = -2.90)
USB_R_CX =  8.35                            # +X port centre  (board y = +8.35)  -> spacing 11.25mm (we had 15.4, too wide)
USB_ZC   = GLASS_TOP_Z - 57.2 + 37.85 - 2.0 # = GLASS_TOP_Z - 21.35. Fit test (2026-10-04): with the screws in, the ports sat
                                            #  ~2mm lower than the holes and the cover had to flex to meet them -> moved 2mm down
USB_PORT_W, USB_PORT_H = 9.5, 4.5           # RECESS/WINDOW footprint reference ONLY (sizes the window + insert body + snap). NOT the plug hole.
USB_HOLE_W, USB_HOLE_H = 9.9, 4.1           # (was 9.5x3.7 = 0.28/side: printed holes shrink, esp. the first layers, so the
                                            #  ports caught on the edges and shoved the insert) -> ~0.5/side + a lead-in bevel
USB_HOLE_LEADIN = 0.6                       # 45deg bevel at the hole mouths (outer 3 edges; the bar between stays full) so
                                            #  the ports slide in and centre the insert. Openings nest over the 8.94x3.16 shells.
USB_SLEEVE = 1.2                            # raised pad around the holes that reaches this much further forward -> the
                                            #  holes sleeve 2.0mm over the shells (the floor itself stays at 0.8: neighbours)
USB_FRONT_ENGAGE = 0.8                      # how far the SOLID front face drops over the port tops. Kept < the height the ports stand proud of their neighbours (you: 1-2mm). Bump toward 1.5 if confirmed ~2mm.
USB_WEB = 1.5                               # thickness of the BAR between the two openings. Kept < the real ~2mm gap between the connectors so
#                                             the openings clear the shells. Only the holes' INNER edges are relieved to hit this (outer edge still hugs the plug).
USB_PORT_Y = FRONT_T + 13.05                 # = 16.05 = the real connector back/opening face (board z=13.05 + Y_off=3.0, derived 4 independent ways, high confidence). Floor (+0.3) lands 0.3mm behind it -> rests on rim, collision-free.
USB_RECESS = True                           # bring the cover IN to the ports (snug & close)
# Finger-access "dent": the RECESS around the holes is bigger than the holes so fingers can grip a USB-C plug
# and push it in. The through-holes are SNUG USB-C-connector-sized; only the dent grows. (z=height, IO
# edge at top -> "below the hole" = lower z, toward the screen.)
USB_FCL_SIDE  = 8.0   # extra recess on EACH side of the holes
USB_FCL_BELOW = 7.0   # extra recess BELOW the holes
USB_FCL_ABOVE = 2.4   # just above the holes: the 2x13 header's plugs sit ~5mm above the port centre (was 4.0 -> the
                      # insert top rammed the dupont plugs and got shoved down when the screws were tightened)
USB_LEADIN   = 4.0                          # funnel mouth grows the opening by this per axis at the outer face (aim lead-in)
USB_LEADIN_D = 5.0                          # funnel depth (LEADIN<=LEADIN_D keeps the funnel wall self-supporting, no support)
# --- sensor placement (derived)
SEN_Y0 = FRONT_T + SEN_GAP_FRONT          # sensor front
SEN_Y1 = SEN_Y0 + SEN_DY                  # ported (back) face
SEN_CX = 0.0
# port centres in CASE coords (sensor centred in X)
INLET_CX = -SEN_DX/2 + SEN_INLET_X
FAN_CX   = -SEN_DX/2 + SEN_FAN_X
BOARD_BACK_Y = FRONT_T + 11.0
# --- Name debossed on the chin, between the screen and the vents ("" = none)
BRAND_TEXT  = "RadarAura"
BRAND_SIZE  = 6.0      # font size (mm); cap height ~0.73x
BRAND_DEPTH = 0.6      # deboss depth into the front face
# --- Radar mark on the back (dot + 2 rings + sweep, name under it), debossed; False = plain back
BACK_ICON      = True
BACK_ICON_ZC   = 82.0   # ring centre: midway between the screw rows, below the USB window
BACK_ICON_R    = 11.0   # outer ring radius
BACK_TEXT_SIZE = 6.0    # name font size under the mark (same as the front; 5 made the letter holes tiny)
BACK_TEXT_GROW = 0.0    # thicken every letter stroke by 2x this (mm) so the slicer fills it solid

# ===================== helpers =====================
def rrect_at(w,d,r):
    r=max(.1,min(r,w/2-.1,d/2-.1)); return sbox(-w/2+r,r,w/2-r,d-r).buffer(r,join_style=1,quad_segs=16)
def wedge(w,d_bot,d_top,z0,z1):
    s=trimesh.creation.extrude_polygon(rrect_at(w,d_bot,R), z1-z0); s.apply_translation((0,0,z0))
    n=np.array([0.0,-(z1-z0),-(d_bot-d_top)]); n/=np.linalg.norm(n)
    return s.slice_plane(plane_origin=[0,d_bot,z0], plane_normal=n, cap=True)
def prism_y(poly,y0,y1,cx=0,zc=0):
    m=trimesh.creation.extrude_polygon(poly,abs(y1-y0))
    m.apply_transform(trimesh.transformations.rotation_matrix(np.pi/2,[1,0,0])); m.apply_translation((cx,y1,zc)); return m
def cyl_y(r,y0,y1,cx,cz):
    c=trimesh.creation.cylinder(radius=r,height=abs(y1-y0),sections=SEG)
    c.apply_transform(trimesh.transformations.rotation_matrix(np.pi/2,[1,0,0])); c.apply_translation((cx,(y0+y1)/2,cz)); return c
def rr_xz(w,h,r,cx=0,cz=0):
    r=max(.1,min(r,w/2-.1,h/2-.1)); return sbox(cx-w/2+r,cz-h/2+r,cx+w/2-r,cz+h/2-r).buffer(r,quad_segs=14)
def zone_d(z):                      # back depth of the zone at height z
    return D_CHIN if z < Z_CHIN else (D_PIN if z >= Z_PIN else D_MID)
def stepped(w, dreduce, zpad=0.0):  # union of the 3 flat-panel depth zones (front at Y=0)
    return union([
        wedge(w, D_CHIN-dreduce, D_CHIN-dreduce, 0-zpad,  Z_CHIN+2),
        wedge(w, D_MID-dreduce,  D_MID-dreduce,  Z_CHIN,  Z_PIN+2),
        wedge(w, D_PIN-dreduce,  D_PIN-dreduce,  Z_PIN,   H+zpad)])
def Dback(z): return zone_d(z)                  # front-BODY outer back depth
def Cout(z):  return zone_d(z) - 0.4            # rear-COVER outer back Y
def snap_geom(z):
    """Key Y coords for a snap finger/window at height z (shared by cover + body so they always match).
    Yroot = finger root (back-panel front face), Ytip = free tip, Yhook = catch face, xf = finger outer X."""
    xf = (W-2*WALL-0.4)/2
    Yseam = Cout(z); Yroot = Yseam - COVER_T; Ytip = Yroot - SNAP_FREE; Yhook = Ytip + 2.0
    return Yroot, Ytip, Yhook, xf
def usb_slot(px, plate_y):
    """Snug USB-C hole (no funnel). The opening is sized to the USB-C plug; it gets shallow because a
    recessed POCKET (added in rear_cover) brings the back surface in close to the port here."""
    y0, y1 = USB_PORT_Y-1.0, plate_y+2.0
    return box(USB_PORT_W, y1-y0, USB_PORT_H, cx=px, cy=(y0+y1)/2, cz=USB_ZC)
# ===== USB-C SNAP-IN INSERT (separate part) — kills the recess "spaghetti" =====
# The deep recess + holes can't print clean as part of the cover (the floor bridges mid-air). So they move to
# a small INSERT printed on its own (floor-down -> crisp holes, no bridge) that snaps into a plain WINDOW in
# the cover. Retention: flush outer FLANGE (rigid push-in stop + locator) + 2 barbs on flex tongues that catch
# behind the 2.4mm cover skin (pull-out stop). The board sits behind as a backstop.
USB_FLOOR_Y  = USB_PORT_Y - USB_FRONT_ENGAGE                      # SOLID front face sits IN FRONT of the port opening -> the ports nest up into the holes (the integrated 'drop over the ports'). Prints flat: the whole face is on the bed, no floating collar.
USB_PLATE_Y  = Cout(USB_ZC)                                       # outer back surface at the USB
_hxL, _hxR   = USB_L_CX - USB_PORT_W/2, USB_R_CX + USB_PORT_W/2
USB_WIN_CX   = ((_hxL-USB_FCL_SIDE)+(_hxR+USB_FCL_SIDE))/2        # window = the old finger footprint
USB_WIN_W    = (_hxR+USB_FCL_SIDE)-(_hxL-USB_FCL_SIDE)
USB_WIN_CZ   = USB_ZC + (USB_FCL_ABOVE-USB_FCL_BELOW)/2
USB_WIN_H    = USB_PORT_H + USB_FCL_ABOVE + USB_FCL_BELOW
USB_INS_WALL_TOP = 1.2                                            # thinner top wall: keeps 6.9mm inside for a 6.5mm plug overmold
USB_INS_WALL, USB_INS_FLOOR = 1.6, 1.0                            # cup wall / floor thickness (floor 1.0mm thin so the 6.5mm plug seats 5.2mm into the port)
USB_FLANGE_T, USB_FLANGE_LIP = 1.6, 1.6                           # flush flange thickness / lip beyond the window
USB_INS_CLR  = 0.10                                               # body-to-window clearance (total) -> 0.05/side snug
# --- snap barbs (STRONGER clip): one cantilever barb per window edge (4 total) clicking behind the OUTER skin face ---
USB_HOOK_PROJ = 0.9     # barb projection beyond the body wall (catch = PROJ-clr behind the window edge) -> firmer hold
USB_HOOK_RISE = 1.2     # barb height in +Y -> ramp x/y = (rd+PROJ)/(rg+RISE) = 1.4/1.7 = 39deg < 45 -> self-supporting
USB_HOOK_W    = 7.0     # tongue/barb width
USB_HOOK_SLOT = 1.2     # slot freeing each side of the tongue (lets it flex inward to snap, then spring back)
USB_HOOK_ANCHOR = 8.0   # tongue anchored this far above the floor (the bit above flexes -> crisp click)
# --- collar: OFF. It sleeved over the ports but printed as a floating floor (spaghetti). The root cause it fought
#     (a loose, wrong-sized board) is now fixed (real 66.8mm board + snug cavity + ribs), so it's no longer needed. ---
USB_COLLAR    = False   # set True to re-enable the port sleeve (needs support under the floor when printed)
USB_COL_WALL  = 1.2     # collar wall
USB_COL_DEPTH = 3.3     # collar sleeve depth -> 3.0mm engagement over the shells (locks the holes onto the ports so they can't slide off). Tune 2.3..4.3 for 2..4mm. Board verified clear to a 5.6mm sleeve.
USB_COL_CLR   = 0.30    # collar-to-shell clearance per side

def usb_insert():
    # Retention = a STRONGER panel snap, captured on BOTH sides of the 2.4mm skin:
    #   * chamfered INNER flange (cavity side) -> push-OUT stop (takes the cable-removal load)
    #   * 4 cantilever barbs (ONE PER WINDOW EDGE) that CLICK behind the EXTERIOR face -> push-IN stop + a firm,
    #     even click with no wobble.  Skin trapped in the flange<->barb groove.  Body snug -> no side-drift.
    # PLUS a front COLLAR that sleeves over BOTH USB-C shells so the insert REGISTERS on the real ports (sits tight).
    # Prints floor-down: every barb ramp is a <45deg down-face (self-supporting); no barb sits in the window at seat.
    pY, fY = USB_PLATE_Y, USB_FLOOR_Y
    cx, cz = USB_WIN_CX, USB_WIN_CZ
    WW, WH = USB_WIN_W, USB_WIN_H
    bw, bh = WW-USB_INS_CLR, WH-USB_INS_CLR                       # snug body (no side-drift)
    yskin  = pY - COVER_T                                         # cover-skin INNER face = flange seat
    fw, fh = WW+2*USB_FLANGE_LIP, WH+2*USB_FLANGE_LIP             # inner flange, wider than the window
    parts, cuts = [], []
    parts.append(box(bw, pY-fY, bh, cx=cx, cy=(fY+pY)/2, cz=cz))  # cup body, outer face flush in the window
    yc0 = yskin - 2.4                                             # 45deg chamfer ramp from body up to the flange
    pts  = [[cx+sx*bw/2, yc0,   cz+sz*bh/2] for sx in (-1,1) for sz in (-1,1)]
    pts += [[cx+sx*fw/2, yskin, cz+sz*fh/2] for sx in (-1,1) for sz in (-1,1)]
    parts.append(trimesh.PointCloud(np.array(pts, float)).convex_hull)            # chamfered flange (push-out stop)
    # --- 4 snap barbs, one per edge. Each is ROOTED 0.5mm into its wall so it MERGES into one solid (no edge-touch
    #     -> would print detached). Protrusion crosses the window edge just past pY -> grabs the OUTER skin face. ---
    rd, rg = 0.5, 0.5
    yr, ya = pY - rg, pY + USB_HOOK_RISE
    def add_barb(wall, sgn, perp, vertical):                      # vertical=False -> protrude in X ; True -> in Z
        wr, wa = wall - sgn*rd, wall + sgn*USB_HOOK_PROJ          # barb root (in wall) / apex (proud)
        P = []
        for w in (perp-USB_HOOK_W/2, perp+USB_HOOK_W/2):
            for u, y in ((wr, yr), (wa, ya), (wr, ya)):
                P.append([w, y, u] if vertical else [u, y, w])
        parts.append(trimesh.PointCloud(np.array(P, float)).convex_hull)          # self-supporting barb (ramp -> flat top)
        slen = 2*(USB_INS_WALL+USB_HOOK_PROJ+1.0)
        ylo, yhi = fY+USB_HOOK_ANCHOR, ya+1
        for s2 in (-1, 1):                                                        # slots free the tongue so it can flex
            o = perp + s2*(USB_HOOK_W/2+USB_HOOK_SLOT/2)
            if vertical: cuts.append(box(USB_HOOK_SLOT, yhi-ylo, slen, cx=o, cy=(ylo+yhi)/2, cz=wall))
            else:        cuts.append(box(slen, yhi-ylo, USB_HOOK_SLOT, cx=wall, cy=(ylo+yhi)/2, cz=o))
    if USB_SLEEVE > 0:                                            # sleeve pad: 1.6 wall around both holes at its front,
        gapC0 = (USB_L_CX+USB_R_CX)/2                             #  45deg flanks out to the floor -> prints without support
        x0 = USB_L_CX-USB_HOLE_W/2-1.6; x1 = USB_R_CX+USB_HOLE_W/2+1.6
        z0 = USB_ZC-USB_HOLE_H/2-1.6;  z1 = USB_ZC+USB_HOLE_H/2+1.6
        S = USB_SLEEVE; P = []
        for y, g in ((fY-S, 0.0), (fY+0.01, S)):
            P += [[x, y, z] for x in (x0-g, x1+g) for z in (z0-g, z1+g)]
        pad = trimesh.PointCloud(np.array(P, float)).convex_hull
        parts.append(pad.intersection(box(bw, 10, bh, cx=cx, cy=fY, cz=cz)))   # never wider than the cup body
    for sgn in (-1, 1):
        add_barb(cx+sgn*bw/2, sgn, cz, vertical=False)           # left / right edges
        add_barb(cz+sgn*bh/2, sgn, cx, vertical=True)            # top / bottom edges
    cuts.append(box(bw-2*USB_INS_WALL, (pY+2)-(fY+USB_INS_FLOOR), bh-USB_INS_WALL-USB_INS_WALL_TOP,
                    cx=cx, cy=((fY+USB_INS_FLOOR)+(pY+2))/2,
                    cz=cz+(USB_INS_WALL-USB_INS_WALL_TOP)/2))                    # hollow cup (open at back)
    gapC = (USB_L_CX+USB_R_CX)/2                                                 # midpoint between the two ports
    for px, sgn in ((USB_L_CX,-1), (USB_R_CX,1)):                                # holes hug the plug on the OUTER edge (px+/-W/2);
        outer, inner = px+sgn*USB_HOLE_W/2, gapC+sgn*USB_WEB/2                   #   the INNER edge is pulled in so only USB_WEB is left between them
        cuts.append(box(abs(outer-inner), (fY+USB_INS_FLOOR+2)-(fY-USB_SLEEVE-2), USB_HOLE_H, cx=(outer+inner)/2, cy=fY+(USB_INS_FLOOR-USB_SLEEVE)/2, cz=USB_ZC))
        hx0, hx1, L = min(outer,inner), max(outer,inner), USB_HOLE_LEADIN
        fy = fY - USB_SLEEVE                                                      # mouth = front of the sleeve pad
        P  = [[x, fy+L, z] for x in (hx0, hx1) for z in (USB_ZC-USB_HOLE_H/2, USB_ZC+USB_HOLE_H/2)]
        ex0, ex1 = (hx0-L-0.5, hx1) if sgn < 0 else (hx0, hx1+L+0.5)          # no bevel on the bar side
        P += [[x, fy-0.5, z] for x in (ex0, ex1) for z in (USB_ZC-USB_HOLE_H/2-L-0.5, USB_ZC+USB_HOLE_H/2+L+0.5)]
        cuts.append(trimesh.PointCloud(np.array(P, float)).convex_hull)          # 45deg lead-in bevel at the mouth
    # --- front COLLAR (optional): a perimeter sleeve over BOTH shells that registers on the real ports.
    #     OFF by default -- it printed as a floating floor. Tracks the port positions when enabled. ---
    if USB_COLLAR:
        SHELL_W, SHELL_H = 8.94, 3.16
        sx0, sx1 = USB_L_CX-SHELL_W/2, USB_R_CX+SHELL_W/2
        sz0, sz1 = USB_ZC-SHELL_H/2, USB_ZC+SHELL_H/2
        ciw, cih = (sx1-sx0)+2*USB_COL_CLR, (sz1-sz0)+2*USB_COL_CLR              # collar inner (clears both shells)
        ccx, ccz = (sx0+sx1)/2, (sz0+sz1)/2
        cy0, cy1 = fY-USB_COL_DEPTH, fY                                          # sleeve forward of the floor
        parts.append(box(ciw+2*USB_COL_WALL, cy1-cy0, cih+2*USB_COL_WALL, cx=ccx, cy=(cy0+cy1)/2, cz=ccz))  # collar outer
        cuts.append(box(ciw, cy1-(cy0-1), cih, cx=ccx, cy=((cy0-1)+cy1)/2, cz=ccz))                          # hollow it
    return diff(union(parts), cuts)

# ===================== NAME =====================
def brand_text():
    """Name solid spanning Y[-1, BRAND_DEPTH] on the chin, readable from the front (face is Y=0)."""
    from matplotlib.textpath import TextPath; from matplotlib.font_manager import FontProperties
    tp=TextPath((0,0),BRAND_TEXT,size=BRAND_SIZE,prop=FontProperties(family="DejaVu Sans",weight="bold")); g=None
    for lp in tp.to_polygons():
        if len(lp)>=3: p=Polygon(lp).buffer(0); g=p if g is None else g.symmetric_difference(p)
    gs=list(g.geoms) if g.geom_type=="MultiPolygon" else [g]
    t=trimesh.util.concatenate([trimesh.creation.extrude_polygon(p,BRAND_DEPTH+1.0) for p in gs if p.area>.01])
    b=t.bounds; t.apply_translation((-(b[0][0]+b[1][0])/2,-(b[0][1]+b[1][1])/2,0))
    # +90 deg about X: text Z-up, readable from the front, extrusion now spans Y[-(d+1), 0]
    t.apply_transform(trimesh.transformations.rotation_matrix(np.pi/2,[1,0,0]))
    t.apply_translation((0,BRAND_DEPTH,WIN_ZC-WIN_Z/2-7.5))    # -> Y[-1, DEPTH]
    return t

def back_icon():
    """Radar mark + name as a solid spanning Y[Cout-BRAND_DEPTH, Cout+1], readable from BEHIND the case."""
    from shapely.geometry import Point
    from shapely.ops import unary_union
    from matplotlib.textpath import TextPath; from matplotlib.font_manager import FontProperties
    R, s = BACK_ICON_R, 1.2                                          # ring stroke 1.2 = 3 lines at 0.4
    ring = lambda ro: Point(0, 0).buffer(ro, 64).difference(Point(0, 0).buffer(ro-s, 64))
    a0, a1 = math.radians(35), math.radians(80)                      # sweep wedge, upper right
    wedge = Polygon([(0, 0)] + [(R*math.cos(a0+(a1-a0)*i/24), R*math.sin(a0+(a1-a0)*i/24)) for i in range(25)])
    mark = unary_union([Point(0, 0).buffer(1.8, 32), ring(R*0.58), ring(R), wedge])
    tp = TextPath((0, 0), BRAND_TEXT, size=BACK_TEXT_SIZE, prop=FontProperties(family="DejaVu Sans", weight="bold")); g = None
    for lp in tp.to_polygons():
        if len(lp) >= 3: p = Polygon(lp).buffer(0); g = p if g is None else g.symmetric_difference(p)
    if BACK_TEXT_GROW: g = g.buffer(BACK_TEXT_GROW, join_style=1)
    b = g.bounds; tx, ty = -(b[0]+b[2])/2, -R-3.5-b[3]                # name centred, 3.5mm under the rings
    from shapely.affinity import translate
    shape = unary_union([mark, translate(g, tx, ty)])
    gs = list(shape.geoms) if shape.geom_type == "MultiPolygon" else [shape]
    t = trimesh.util.concatenate([trimesh.creation.extrude_polygon(p, BRAND_DEPTH+1.0) for p in gs if p.area > .01])
    y0 = Cout(BACK_ICON_ZC) - BRAND_DEPTH
    # (u,v,w) -> (x=-u, y=y0+w, z=zc+v): seen from behind (looking toward -Y) u points right
    t.apply_transform(np.array([[-1, 0, 0, 0], [0, 0, 1, y0], [0, 1, 0, BACK_ICON_ZC], [0, 0, 0, 1]], float))
    return t

def back_inlay():
    """The back recess's exact fill: flush mark for a two-colour cover."""
    c = Cout(BACK_ICON_ZC)
    return back_icon().intersection(box(W, BRAND_DEPTH, H, cy=c-BRAND_DEPTH/2, cz=H/2))

def name_inlay():
    """The recess's exact fill (Y 0..BRAND_DEPTH): flush letters for a two-colour print."""
    return brand_text().intersection(box(W, BRAND_DEPTH, H, cy=BRAND_DEPTH/2, cz=H/2))

# ===================== FRONT BODY =====================
def front_body():
    outer = stepped(W, 0.0)                                     # flat-backed box (3 equal depth zones)
    inner = wedge(W-2*WALL, D_CHIN+8, D_CHIN+8, WALL, H-WALL)    # uniform-deep cavity: CLOSED top+bottom, OPEN back
    inner.apply_translation((0, FRONT_T, 0))
    body = outer.difference(inner)
    adds, cuts = [], []

    cuts.append(prism_y(rr_xz(WIN_X,WIN_Z,0.5,cz=WIN_ZC), -1, FRONT_T+1))    # screen window — SQUARE corners (r0.5) to match the rectangular lit area (was r4 = too round)

    # SNAP POCKETS: a BLIND recess (NOT through the wall) in each CHIN side wall that the cover's snap hook
    # springs into; the pocket's back edge is the catch ledge. Blind -> the sensor chamber stays sealed and
    # there are no holes on the case exterior. (~1.2mm pocket into the 2.4mm wall, leaving ~1.2mm.)
    for sx in (-1, 1):
        for z in SNAP_ZS:
            Yroot, Ytip, Yhook, xf = snap_geom(z)
            ylo, yhi = Ytip-0.2, Yhook+0.05  # near-zero pull-out play -> catch bites immediately, no in/out wobble
            xi = (W-2*WALL)/2; pdepth = SNAP_HO
            cuts.append(box(pdepth, yhi-ylo, SNAP_FW+0.2, cx=sx*(xi+pdepth/2), cy=(ylo+yhi)/2, cz=z))  # 1.2 -> 0.4 -> now +0.2: only 0.1mm/side, grips the finger so it can't drift (open a hair if it won't seat)

    # shelf: chamber ceiling / board floor (vertical wall in-print -> support free)
    sh_d = Dback(SHELF_Z)-FRONT_T-COVER_T-0.5
    adds.append(box(W-2*WALL, sh_d, 2.5, cx=0, cy=FRONT_T+sh_d/2, cz=SHELF_Z))
    # WIRE NOTCH through the shelf's back edge, directly over the SEN66 connector (x -8.6..+2.3): the cable
    # drops from the board cavity straight down onto the connector. (The flat-back shelf now spans nearly the
    # full depth -> without this notch the chamber is sealed and the cable has NO path to the sensor.)
    cuts.append(box(12.0, 10.0, 6.0, cx=(SEN_CONN_X0+SEN_CONN_X1)/2, cy=FRONT_T+sh_d-4.5, cz=SHELF_Z))

    # sensor cradle: 2 side ribs (cap->shelf) hug the sensor; floor is the closed bottom.
    # Each rib carries an END-CLIP: two slots free a horizontal tongue (z 10.2..20.2, rooted at the front)
    # whose 45deg nub clicks into the SEN6x mounting recess on the sensor's end face. ~1% strain at full
    # deflection; the sensor's own 1.2mm ported-face rim chamfer is the insertion lead-in.
    zc_t = SEN_Z0 + SEN_DZ/2                    # tongue/nub centre = sensor mid-height (recess ~mid-face)
    for sx in (-(SEN_DX/2+SEN_RIB_CLR), (SEN_DX/2+SEN_RIB_CLR)):
        rib_d = SEN_DY + 1.0
        s = np.sign(sx)
        adds.append(box(1.6, rib_d, SHELF_Z-WALL, cx=sx+s*0.8, cy=SEN_Y0+rib_d/2, cz=(WALL+SHELF_Z)/2))
        for zs in (zc_t-5.0-0.6, zc_t+5.0+0.6):    # 1.2mm slots above+below the tongue, open at the rib back
            cuts.append(box(4.0, (SEN_Y0+rib_d+1.0)-8.0, 1.2, cx=sx+s*0.8, cy=(8.0+SEN_Y0+rib_d+1.0)/2, cz=zs))
        h, p = SEN_CLIP_NUB/2, SEN_CLIP_PROUD      # nub: 45deg frustum, base rooted 0.4 INTO the tongue
        xf = sx                                     # rib inner face x
        pts = ([(xf+s*0.4, SEN_CLIP_YC+dy, zc_t+dz) for dy in (-h, h) for dz in (-h, h)] +
               [(xf-s*p, SEN_CLIP_YC+dy, zc_t+dz) for dy in (-(h-p), h-p) for dz in (-(h-p), h-p)])
        adds.append(trimesh.PointCloud(np.array(pts, float)).convex_hull)

    # BOARD-LOCATING RIBS: thin ribs on each side wall that hug the PCB edges so the board can't slide in the
    # cavity (was 0.40mm/side free). 2 per side at the screw heights -> located where it's also screwed.
    x_in  = (W-2*WALL)/2                                         # cavity wall inner X
    x_rib = PCB_X/2 + BOARD_RIB_CLR                             # rib tip = snug to the PCB edge
    rib_w = (x_in-x_rib) + 1.0                                  # protrude to x_rib, overlap 1mm into the wall
    for sx in (-1, 1):
        for rz in sorted(set(s[1] for s in SC)):
            adds.append(box(rib_w, (BOARD_BACK_Y+1)-FRONT_T, 24,
                            cx=sx*(x_rib+rib_w/2), cy=(FRONT_T+BOARD_BACK_Y+1)/2, cz=rz))

    # Name recessed into the chin; name_inlay() fills the recess (print it in a second colour)
    if BRAND_TEXT:
        cuts.append(brand_text())

    # FRONT vents matching the SEN66 drawing EXACTLY (centres from the drawing, +0.4mm openings):
    #   square 7x7 @ (x6.7,y6.3) | Ø6 @ (x6.7,y18.2) | Ø20.5 fan @ (x42.4,y12.8)
    sq_cz  = SEN_Z0 + SEN_DZ - 6.3      # square centre (drawing y=6.3)
    h6_cz  = SEN_Z0 + SEN_DZ - 18.2     # Ø6 centre (drawing y=18.2)  [was wrongly 15.5]
    cuts.append(prism_y(rr_xz(7.4, 7.4, 1.0, cx=INLET_CX, cz=sq_cz), -1, FRONT_T+1))   # square 7.0 +0.4
    cuts.append(cyl_y(3.2, -1, FRONT_T+1, INLET_CX, h6_cz))                            # Ø6.4 (Ø6 +0.4)
    cuts.append(cyl_y(SEN_FAN_D/2+0.4, -1, FRONT_T+1, FAN_CX, SEN_ZC))                 # Ø21.3 (Ø20.5 +0.4)
    # (baffle removed: with the ported face flush on the wall, each port mates its own opening directly —
    # there is no shared plenum for exhaust to recirculate through, so nothing to split or foam-seal.)

    return diff(union([body]+adds), cuts)

# ===================== REAR COVER =====================
def rear_cover():
    # back skin at the TRUE back of the case (outer ~0.4mm inside the body's outer back)
    o = stepped(W-2*WALL-0.4, 0.4)
    cover = o.difference(o.copy().apply_translation((0, -COVER_T, 0)))   # shell the BACK only
    # Uniform flat back -> one continuous back panel; the old wire-conduit bump + step-ledge bridges are
    # gone with the steps (no longer needed, and they went degenerate at uniform depth).

    # LOCATING is by the back panel itself: it plugs into the cavity opening (x±34.9 in a ±35.1 wall = 0.2mm
    # slip) and sits BEHIND the board (y>=17.2, behind the ~13mm side components), so it registers the whole
    # perimeter board-safely. NO forward skirt alongside the board (that's exactly where the side components
    # are). The CHIN snaps + 4 metal screws hold it. (The earlier full-perimeter skirt reached into those
    # side components -> removed.)

    # SNAP FINGERS (in the CHIN, where the sides are board-free): standalone cantilevers off the back panel
    # reaching forward into the deep chin; the hooked tip springs into the body's BLIND POCKET = the click/
    # lock. Sensor is central so the sides are clear; no slots needed (each finger is already free-standing).
    hadds = []
    for sx in (-1, 1):
        for z in SNAP_ZS:
            Yroot, Ytip, Yhook, xf = snap_geom(z)
            hadds.append(box(SNAP_T, Yroot-Ytip, SNAP_FW, cx=sx*(xf-SNAP_T/2), cy=(Ytip+Yroot)/2, cz=z))    # finger beam
            poly = Polygon([(sx*xf, Ytip), (sx*xf, Yhook), (sx*(xf+SNAP_HO), Yhook)])                        # ramp + catch hook
            hk = trimesh.creation.extrude_polygon(poly, SNAP_FW); hk.apply_translation((0, 0, z-SNAP_FW/2))
            hadds.append(hk)
    cover = union([cover]+hadds)

    adds, cuts = [], []

    # 4 board-mount posts: from the cover forward to the board standoff; the M2 passes through
    # the Ø2.3 clearance bore and threads INTO the board. Tightening pulls the board fwd to the bezel.
    for sx,sz in SC:
        adds.append(cyl_y(3.8, STANDOFF_Y, Cout(sz), sx, sz))               # Ø7.6 guide/standoff post
        cuts.append(cyl_y(SCREW_CLEAR_R, STANDOFF_Y-1.0, Cout(sz)+2, sx, sz))  # M2 clearance, through post + cover
        cuts.append(cyl_y(SCREW_HEAD_R, Cout(sz)-SCREW_HEAD_D, Cout(sz)+1, sx, sz))  # head counterbore
        cone = trimesh.creation.cone(radius=SCREW_HEAD_R, height=SCREW_HEAD_R, sections=SEG)  # 45deg seat
        cone.apply_transform(trimesh.transformations.rotation_matrix(np.pi/2,[1,0,0]))     # apex toward -Y
        cone.apply_translation((sx, Cout(sz)-SCREW_HEAD_D+0.01, sz)); cuts.append(cone)

    # SEN66 SPRING PUSHERS: 2 leaf-spring blades (40deg from the print axis -> self-supporting) rooted in
    # the back panel. Each flat tip lands SEN_SPRING_PRE PAST the sensor's back plane, so closing the cover
    # bends the blade and presses the sensor flush against the front wall — continuous preload, so print
    # tolerance only changes the force, never gives rattle or a cover that won't close. At +/-SEN_PUSH_X
    # they land on the flat label / fan-housing areas, clear of the connector (x -8.6..+2.3) and the cable.
    tip_y = SEN_Y1 - SEN_SPRING_PRE
    dd = D_CHIN - 38.0                                                 # extra depth vs the original 38mm case
    a  = math.radians(40); ax = np.array([math.cos(a), -math.sin(a)])  # blade axis (y,z), toward the root
    bL = 26.0 + dd/math.cos(a)                                         # longer blade so the root still meets the panel
    yc, zc = np.array([30.0+dd, 10.63]) + 13.0*ax - (bL/2)*ax          # root end kept at the panel (shifted by dd)
    for sx in (-1, 1):
        bl = box(8.0, bL, 1.6)
        bl.apply_transform(trimesh.transformations.rotation_matrix(-a, [1, 0, 0]))
        bl.apply_translation((sx*SEN_PUSH_X, yc, zc))                 # tip at y=tip_y, root buried in panel
        ry = 37.0 + dd
        bl = bl.intersection(box(12.0, ry-tip_y, 40.0, cx=sx*SEN_PUSH_X, cy=(tip_y+ry)/2, cz=12.0))  # flat tip face at tip_y
        adds.append(bl)

    # USB WINDOW — just a plain rectangular opening in the back skin. The deep recess + snug holes now live on
    # a separate snap-in INSERT (usb_insert), so the cover has NOTHING to bridge here = no recess "spaghetti".
    # The outer face gets a shallow REBATE so the insert's flange sits flush with the back.
    plate_y = Cout(USB_ZC)
    cuts.append(box(USB_WIN_W, (plate_y+1)-(plate_y-COVER_T-1), USB_WIN_H,
                    cx=USB_WIN_CX, cy=plate_y-COVER_T/2, cz=USB_WIN_CZ))            # window through the skin
    if BACK_ICON:
        cuts.append(back_icon())                                                    # radar mark, debossed
    # no rebate -> the back skin stays full thickness around the window (nothing bridges here), and the insert
    # flange sits PROUD on the outer face (a slim raised bezel) while its barbs grab the full 2.4mm skin.

    cover = diff(union([cover]+adds), cuts)
    # Clip the cover to seat BETWEEN the body's solid top/bottom end-caps (which span only
    # Z[0,WALL] and Z[H-WALL,H]). Full-height the cover's 2.4mm back slabs would ram those caps
    # (verified 804mm^3 overlap, 402 each end). Z[WALL+gap, H-WALL-gap] -> 0 interference.
    zlo, zhi = WALL+COVER_Z_GAP, H-WALL-COVER_Z_GAP
    cover = cover.intersection(box(W+20, D_CHIN+40, zhi-zlo, cy=D_CHIN/2, cz=(zlo+zhi)/2))  # clip Z only; span full Y
    return cover

# ===================== ghosts (render/verify only) =====================
def ghost_sensor():
    return box(SEN_DX, SEN_DY, SEN_DZ, cx=SEN_CX, cy=(SEN_Y0+SEN_Y1)/2, cz=SEN_ZC)

if __name__=="__main__":
    import os
    OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl")
    os.makedirs(OUT, exist_ok=True)
    fb=front_body(); cov=rear_cover(); ins=usb_insert()
    def lay_flat(m, ang, ref=None):
        """Rotate about X into print orientation and drop onto the bed (ref: share another part's offset)."""
        m=m.copy(); R=trimesh.transformations.rotation_matrix(ang,[1,0,0]); m.apply_transform(R)
        r=ref if ref is not None else m
        if ref is not None: r=ref.copy(); r.apply_transform(R)
        m.apply_translation((0,0,-r.bounds[0][2])); return m
    # print-ready: front body screen-face DOWN, rear cover back DOWN, USB insert floor DOWN
    parts=[("v4_front_body", lay_flat(fb, np.pi/2)),
           ("v4_rear_cover", lay_flat(cov, -np.pi/2)),
           ("v4_usb_insert", lay_flat(ins, np.pi/2))]
    if BRAND_TEXT:   # second-colour name: same transform as the body so it sits exactly in its recess
        parts.append(("v4_front_body_name", lay_flat(name_inlay(), np.pi/2, ref=fb)))
    if BACK_ICON:    # second-colour radar mark for the back
        parts.append(("v4_rear_cover_icon", lay_flat(back_inlay(), -np.pi/2, ref=cov)))
    for n,m in parts:
        d=m.bounds[1]-m.bounds[0]
        print(f"  {n:18s} {d[0]:5.1f} x {d[1]:5.1f} x {d[2]:6.1f} | watertight={m.is_watertight} | bodies={m.body_count} | tris={len(m.faces)}")
        m.export(os.path.join(OUT, f"{n}.stl"))
    gs=ghost_sensor()
    vf=fb.intersection(gs); vc=cov.intersection(gs)
    vf=0 if vf.is_empty else vf.volume; vc=0 if vc.is_empty else vc.volume
    print(f"  SEN66 ghost: overlap front_body={vf:.0f} (cradle ribs ok if small), rear_cover={vc:.0f} mm^3")
    print(f"  case {W:.0f} x {H:.0f} x {D_BOT:.0f}mm | chamber Z {WALL:.1f}..{SHELF_Z:.1f} | sensor Z {SEN_Z0:.1f}..{SEN_Z0+SEN_DZ:.1f}")
    print(f"  USB: 2 ports x[{USB_L_CX:.1f},{USB_R_CX:.1f}] z={USB_ZC:.1f}, plug holes {USB_HOLE_W}x{USB_HOLE_H} (footprint {USB_PORT_W}x{USB_PORT_H}), recess {Cout(USB_ZC)-USB_PORT_Y:.1f}mm | intake x={INLET_CX:.1f} exhaust x={FAN_CX:.1f}")
    print(f"  M2 board screws thread INTO the board (case = Ø{2*SCREW_CLEAR_R:.1f} clearance). head on cover -> standoff Y={STANDOFF_Y:.1f}:")
    for sz in sorted(set(s[1] for s in SC)):
        L=Cout(sz)-(SCREW_HEAD_D+0.3)-STANDOFF_Y+6   # span from the counterbore seat + ~6mm thread engagement
        print(f"     z={sz:.0f}: cover Y={Cout(sz):.1f} -> ~M2x{round(L):d}mm")
    # mount-test coupon: one corner post + bore, to try a real M2 screw into the board before the full case
    cz=min(s[1] for s in SC)
    coup=cov.intersection(box(30, Cout(cz)+6, 30, cx=-SC_X, cy=(Cout(cz)+6)/2, cz=cz))
    # lay it back-face-down (post pointing up): rests on a ~30x30 flat face instead of a thin
    # standing edge, so it doesn't lift/curl off the plate like the failed print did.
    coup.apply_transform(trimesh.transformations.rotation_matrix(-math.pi/2,[1,0,0]))  # +Y -> -Z
    coup.apply_translation((0,0,-coup.bounds[0][2]))                                    # drop to bed
    coup.export(os.path.join(OUT, "v4_screw_coupon.stl"))
    print(f"  wrote v4_screw_coupon.stl (post-up flat, watertight={coup.is_watertight})")
