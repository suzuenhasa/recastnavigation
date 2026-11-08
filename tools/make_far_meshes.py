"""Writes copies of the RecastDemo meshes moved 100,000 units from the origin (x and z).

usage: make_far_meshes.py <RecastDemo/Bin/Meshes> <out_dir>
"""
import sys
from pathlib import Path

src, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
for name in ("dungeon", "nav_test", "undulating"):
    with open(src / f"{name}.obj") as f, open(out / f"{name}_far.obj", "w") as g:
        for line in f:
            if line.startswith("v "):
                x, y, z = map(float, line.split()[1:4])
                g.write(f"v {x + 100000.0:.4f} {y:.4f} {z + 100000.0:.4f}\n")
            else:
                g.write(line)
