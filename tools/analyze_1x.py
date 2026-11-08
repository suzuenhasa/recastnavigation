"""Compare stock (2 x climb) with the 1 x climb variant on the sweep in results/sweep1x.

For every link that 1x removes, sample the source geometry just inside each of the two tiles at the portal
middle and take the surface closest to each polygon's height. If the two surfaces are within walkableClimb of
each other, an agent can step across there, so removing the link breaks a real connection. Otherwise the
removal is right (a ledge higher than the climb, or separate floors).

usage: analyze_1x.py meshes_dir sweep_dir
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

meshes, sweep = Path(sys.argv[1]), Path(sys.argv[2])
OFF = 0.15  # sample half a cell inside each tile
SIDE = {0: (1, 0), 2: (0, 1), 4: (-1, 0), 6: (0, -1)}  # link side -> (dx, dz) towards the target tile


def load_obj(path):
    v, f = [], []
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            v.append(list(map(float, p[1:4])))
        elif p[0] == "f":
            idx = [int(t.split("/")[0]) for t in p[1:]]
            idx = [i - 1 if i > 0 else len(v) + i for i in idx]
            for k in range(2, len(idx)):
                f.append((idx[0], idx[k - 1], idx[k]))
    v = np.array(v)
    t = v[np.array(f)]
    n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
    keep = np.abs(n[:, 1]) > 1e-6 * np.linalg.norm(n, axis=1).clip(1e-12)
    return t[keep]


def heights_at(tris, x, z):
    """Heights of all non-vertical triangles above/below (x, z)."""
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    d = (b[:, 2] - c[:, 2]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 2] - c[:, 2])
    l1 = ((b[:, 2] - c[:, 2]) * (x - c[:, 0]) + (c[:, 0] - b[:, 0]) * (z - c[:, 2])) / d
    l2 = ((c[:, 2] - a[:, 2]) * (x - c[:, 0]) + (a[:, 0] - c[:, 0]) * (z - c[:, 2])) / d
    l3 = 1 - l1 - l2
    inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
    return np.sort(l1[inside] * a[inside, 1] + l2[inside] * b[inside, 1] + l3[inside] * c[inside, 1])


def parse(path):
    links, gaps, queries, slabs = {}, {}, {}, {}
    last = None
    for line in open(path):
        if line.startswith("link "):
            m = re.match(r"link (\S+) p(\d+) e(\d+) -> (\S+) p(\d+) side(\d+) portal", line)
            last = (m[1], m[2], m[3], m[4], m[5])
            links[last] = int(m[6])
        elif line.startswith("slab "):
            dmin, dmax = map(float, line.split()[1:3])
            slabs[last] = 0.0 if dmin * dmax < 0 else min(abs(dmin), abs(dmax))
        elif line.startswith("gap "):
            m = re.search(r"at (\S+) (\S+) from (\S+) to (\S+)", line)
            gaps[last] = tuple(map(float, m.groups()))
        elif line.startswith("query "):
            f = line.split()
            queries[int(f[1])] = (int(f[5]), float(f[7]))
    return links, gaps, queries, slabs


tris_cache = {}
headroom = []
rows = []
lost_detail = []
for log in sorted(sweep.glob("*.stock.txt")):
    tag = log.name[:-len(".stock.txt")]
    mesh, ts, c = re.match(r"(.+)_t(\d+)_c([\d.]+)", tag).groups()
    climb = float(c)
    if mesh not in tris_cache:
        tris_cache[mesh] = load_obj(meshes / f"{mesh}.obj")
    tris = tris_cache[mesh]
    sl, sg, sq, ss = parse(log)
    xl, _, xq, _ = parse(sweep / f"{tag}.1x.txt")
    lost = [k for k in sl if k not in xl]
    gained = [k for k in xl if k not in sl]
    walkable_step, ledge, unknown = 0, 0, 0
    for k in lost:
        mx, mz, hf, ht = sg[k]
        dx, dz = SIDE[sl[k]]
        sf = heights_at(tris, mx - dx * OFF, mz - dz * OFF)
        st = heights_at(tris, mx + dx * OFF, mz + dz * OFF)
        if len(sf) == 0 or len(st) == 0:
            unknown += 1
            continue
        gf = sf[np.argmin(np.abs(sf - hf))]
        gt = st[np.argmin(np.abs(st - ht))]
        step = abs(gf - gt)
        if step <= climb:
            walkable_step += 1
        else:
            ledge += 1
        lost_detail.append((tag, k, round(abs(hf - ht), 2), round(step, 2), "walkable" if step <= climb else "ledge"))
    for k in sl:
        e = ss.get(k)
        if e is None or e <= 0.5 * climb:
            continue
        mx, mz, hf, ht = sg[k]
        dx, dz = SIDE[sl[k]]
        sf = heights_at(tris, mx - dx * OFF, mz - dz * OFF)
        st = heights_at(tris, mx + dx * OFF, mz + dz * OFF)
        if len(sf) == 0 or len(st) == 0:
            continue
        gf = sf[np.argmin(np.abs(sf - hf))]
        gt = st[np.argmin(np.abs(st - ht))]
        if abs(gf - gt) <= climb:
            headroom.append((tag, climb, e / climb, k))
    reached_lost = sum(1 for n in sq if sq[n][0] == 1 and xq[n][0] == 0)
    reached_gained = sum(1 for n in sq if sq[n][0] == 0 and xq[n][0] == 1)
    longer = sum(1 for n in sq if sq[n][0] == 1 and xq[n][0] == 1 and xq[n][1] > sq[n][1] * 1.01 + 0.01)
    rows.append((mesh, int(ts), climb, len(sl) // 2, len(lost), len(gained), walkable_step, ledge, unknown,
                 sum(q[0] for q in sq.values()), reached_lost, reached_gained, longer))

print(f"{'mesh':<11}{'tile':>5}{'climb':>6}{'links':>7}{'1x lost':>8}{'gained':>7}{'walkable':>9}{'ledge':>6}{'?':>4}"
      f"{'reached':>8}{'q lost':>7}{'q gain':>7}{'longer':>7}")
for r in rows:
    print(f"{r[0]:<11}{r[1]:>5}{r[2]:>6}{r[3]:>7}{r[4]:>8}{r[5]:>7}{r[6]:>9}{r[7]:>6}{r[8]:>4}{r[9]:>8}{r[10]:>7}{r[11]:>7}{r[12]:>7}")
tot = [sum(r[i] for r in rows) for i in range(4, 13)]
print("totals: lost links %d, gained %d, walkable-step %d, ledge %d, unknown %d; queries lost %d, gained %d, longer %d" % (
    tot[0], tot[1], tot[2], tot[3], tot[4], tot[6], tot[7], tot[8]))
print()
print("Links across walkable ground (geometry step <= climb) whose end gap is over half the climb:")
by_climb = defaultdict(list)
for tag, climb, r, k in headroom:
    by_climb[climb].append(r)
for c in sorted(by_climb):
    rs = by_climb[c]
    print(f"  climb {c}: {len(rs)} links, worst gap = {max(rs):.2f} x climb, over 1 x climb: {sum(r > 1 for r in rs)}")
if not headroom:
    print("  none")
with open(sweep / "headroom.tsv", "w") as f:
    f.write("config\tclimb\tgap_over_climb\tlink\n")
    for d in sorted(headroom, key=lambda d: -d[2]):
        f.write("\t".join(map(str, d)) + "\n")
with open(sweep / "lost_links.tsv", "w") as f:
    f.write("config\tlink\tnavmesh_gap\tgeometry_step\tclass\n")
    for d in lost_detail:
        f.write("\t".join(map(str, d)) + "\n")
