# Copyright (c) 2026 DankBuild - RadarAura. RadarAura Build Licence 1.0 - see LICENSE.md
"""Tilted desk stand for the RadarAura case (atmo4.py). The case drops in from above.

Holds the case by its sides, a 3 mm lip front and back (below the vents, which start
5 mm up) and a back crossbar between the rear vents and the USB ports, so airflow and
the cable stay free. Prints flat on its base with no supports (every underside >= 45 deg).

    python stand.py            -> stl/stand_15deg.stl, stl/stand_25deg.stl
    python stand.py 20         -> stl/stand_20deg.stl
"""
import sys
import numpy as np
import trimesh
from shapely.geometry import MultiPoint, Polygon

import atmo4 as case

ANGLES = [15, 25]     # tilt back from vertical (deg)
CLR    = 0.4          # case-to-stand clearance per side
T      = 3.0          # wall / floor thickness
LIP    = 3.0          # front/back lip height (case vents start at z=5)
CHEEK_H = 25.0        # side cheek height at the front of the case
BAR_Z  = (44.0, 74.0) # back crossbar contact band (rear vents end 25.5, USB recess starts ~117)

CW, CD = case.W, case.D_CHIN                  # case footprint (x, y); front face at y=0
XI = CW/2 + CLR                        # inner face of the cheeks
XO = XI + T                            # outer face
Y0, Y1 = -CLR, CD + CLR                # pocket front/back


def tilt(pts, deg):
    """Case frame (y back, z up) -> stand frame, top leaning back by deg."""
    a = np.radians(deg); p = np.asarray(pts, float)
    return np.c_[p[:, 0]*np.cos(a) + p[:, 1]*np.sin(a), -p[:, 0]*np.sin(a) + p[:, 1]*np.cos(a)]


def rect(y0, y1, z0, z1):
    return [(y0, z0), (y1, z0), (y1, z1), (y0, z1)]


def to_ground(poly_pts, deg):
    """Tilted outline + its drop to the ground (z=0 later): the solid below it, so nothing overhangs."""
    p = tilt(poly_pts, deg)
    return np.vstack([p, np.c_[p[:, 0], np.full(len(p), -1e3)]])


def profile_x(pts2d, x0, x1):
    """Extrude a (y,z) outline along X between x0 and x1."""
    poly = MultiPoint([tuple(p) for p in pts2d]).convex_hull if not isinstance(pts2d, Polygon) else pts2d
    m = trimesh.creation.extrude_polygon(poly, x1 - x0)      # polygon in XY, extruded along +Z
    # map (px,py,pz) = (y, z, x) -> (x, y, z)
    m.apply_transform(np.array([[0, 0, 1, x0], [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]], float))
    return m


def stand(deg):
    floor  = to_ground(rect(Y0 - T, Y1 + T, -T, 0), deg)
    cheek  = to_ground(rect(Y0 - T, Y1 + T, -T, CHEEK_H) + rect(Y1, Y1 + T, CHEEK_H, BAR_Z[1]), deg)
    # crossbar: contact face on the case back; its lower edge steps down along the case so the
    # underside stays >= 45 deg in the printed orientation
    drop = T / np.tan(np.radians(45 - deg)) if deg < 45 else 99
    bar = tilt([(Y1, BAR_Z[0] - drop), (Y1 + T, BAR_Z[0]), (Y1 + T, BAR_Z[1]), (Y1, BAR_Z[1])], deg)
    lips = [tilt(rect(Y0 - T, Y0, -T, LIP), deg), tilt(rect(Y1, Y1 + T, -T, LIP), deg)]

    parts = [profile_x(floor, -XO, XO), profile_x(bar, -XO, XO),
             profile_x(cheek, XI, XO), profile_x(cheek, -XO, -XI)]
    parts += [profile_x(l, -XO, XO) for l in lips]
    m = parts[0]
    for p in parts[1:]: m = m.union(p)

    # flatten the bottom: cut at the lowest point of the tilted floor
    zmin = tilt(rect(Y0 - T, Y1 + T, -T, 0), deg)[:, 1].min()
    keep = trimesh.creation.box(extents=(500, 500, 500)); keep.apply_translation((0, 0, zmin + 250))
    m = m.intersection(keep)
    m.apply_translation((0, 0, -zmin))
    return m


if __name__ == "__main__":
    angles = [float(a) for a in sys.argv[1:]] or ANGLES
    for a in angles:
        m = stand(a)
        name = f"stl/stand_{a:g}deg.stl"
        m.export(name)
        e = m.extents
        print(f"{name}: {e[0]:.1f} x {e[1]:.1f} x {e[2]:.1f} mm  watertight={m.is_watertight} "
              f"bodies={m.body_count}  vol={m.volume/1000:.1f} cm3")
