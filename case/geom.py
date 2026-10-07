# Copyright (c) 2026 DankBuild - RadarAura. RadarAura Build Licence 1.0 - see LICENSE.md
"""Small mesh helpers (trimesh + manifold3d) used by atmo4.py."""
import trimesh

SEG = 64

def box(x, y, z, cx=0, cy=0, cz=0):
    b = trimesh.creation.box(extents=(x, y, z)); b.apply_translation((cx, cy, cz)); return b

def cyl(r, h, cx=0, cy=0, cz=0, sections=SEG):
    c = trimesh.creation.cylinder(radius=r, height=h, sections=sections)
    c.apply_translation((cx, cy, cz)); return c

def union(ms):
    o = ms[0]
    for m in ms[1:]: o = o.union(m)
    return o

def diff(a, cs):
    o = a
    for c in cs: o = o.difference(c)
    return o

