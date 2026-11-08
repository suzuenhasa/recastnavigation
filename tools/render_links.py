"""Render navmesh polygons, links between tiles and an optional path from linkdump DUMP_GEOM output,
two builds side by side.

usage: render_links.py out.png "title" left.geom "left title" right.geom "right title" [options]
  --box x0 x1 z0 z1   only draw polygons with a vertex inside this xz box
  --view elev azim    camera angles
  --border x          draw the tile border plane at this x
  --caption "text"
"""
import argparse
import re
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("title")
ap.add_argument("left")
ap.add_argument("left_title")
ap.add_argument("right")
ap.add_argument("right_title")
ap.add_argument("--box", nargs=4, type=float)
ap.add_argument("--view", nargs=2, type=float, default=[22, -58])
ap.add_argument("--border", type=float, action="append", default=[])
ap.add_argument("--caption", default="")
ap.add_argument("--side", action="store_true", help="2D side view: x (or --side-axis z) across, y up")
ap.add_argument("--side-axis", default="x", choices=["x", "z"])
ap.add_argument("--labels", nargs=2, default=["lower floor", "upper floor"])
ap.add_argument("--floor-split", type=float, help="colour polygons below/above this height differently")
a = ap.parse_args()


def load(path):
    polys, portals, path_pts, status = {}, [], [], None
    for line in open(path):
        f = line.split()
        if not f:
            continue
        if f[0] == "poly":
            v = list(map(float, f[3:]))
            polys[(f[1], f[2])] = [v[i:i + 3] for i in range(0, len(v), 3)]
        elif f[0] == "portal":
            portals.append(((f[1], f[2]), (f[4], f[5]), list(map(float, f[6:12]))))
        elif f[0] == "path":
            status = f[1]
            v = list(map(float, f[2:]))
            path_pts = [v[i:i + 3] for i in range(0, len(v), 3)]
    return polys, portals, path_pts, status


def center(vs):
    n = len(vs)
    return [sum(v[i] for v in vs) / n for i in range(3)]


def inside(vs):
    if not a.box:
        return True
    x0, x1, z0, z1 = a.box
    return any(x0 <= v[0] <= x1 and z0 <= v[2] <= z1 for v in vs)


left, right = load(a.left), load(a.right)
right_links = {(p[0], p[1]) for p in right[1]}
left_links = {(p[0], p[1]) for p in left[1]}

# Shared axis limits from the polygons drawn.
pts = [v for polys in (left[0], right[0]) for vs in polys.values() if inside(vs) for v in vs]
xs, ys, zs = zip(*pts)
lim = [(min(xs), max(xs)), (min(zs), max(zs)), (min(ys), max(ys))]

if a.side:
    H = 0 if a.side_axis == "x" else 2
    hlim = lim[0] if H == 0 else lim[1]
    fig, axes = plt.subplots(1, 2, figsize=(14, 5.2), dpi=130, sharey=True)
    fig.suptitle(a.title, fontsize=14, fontweight="bold", y=0.98)
    for ax, (polys, portals, path_pts, status), sub, other in [
            (axes[0], left, a.left_title, right_links), (axes[1], right, a.right_title, left_links)]:
        ax.set_title(sub, fontsize=11.5)
        for key, vs in polys.items():
            if not inside(vs):
                continue
            h = sum(v[1] for v in vs) / len(vs)
            col = (0.35, 0.6, 0.95) if a.floor_split is None or h < a.floor_split else (0.4, 0.75, 0.4)
            hs = [v[H] for v in vs]
            if max(v[1] for v in vs) - min(v[1] for v in vs) < 0.05:
                ax.plot([min(hs), max(hs)], [h, h], color=col, linewidth=9, solid_capstyle="butt", zorder=1)
            else:
                ax.fill(hs, [v[1] for v in vs], facecolor=col + (0.3,), edgecolor=col, linewidth=1.2, zorder=1)
        for bx in a.border:
            ax.axvline(bx, color=(0.45, 0.45, 0.45), linestyle="--", linewidth=1)
            ax.text(bx, lim[2][1] + 0.75, "tile border", ha="center", fontsize=9, color=(0.35, 0.35, 0.35))
        seen = set()
        for frm, to, seg in portals:
            pair = tuple(sorted([frm, to]))
            if pair in seen or frm not in polys or to not in polys:
                continue
            if not (inside(polys[frm]) and inside(polys[to])):
                continue
            seen.add(pair)
            c0, c1 = center(polys[frm]), center(polys[to])
            both = (frm, to) in other
            off = 0.14 if abs(c0[1] - c1[1]) < 0.01 else 0.0
            ax.plot([c0[H], c1[H]], [c0[1] + off, c1[1] + off], color=(0.15, 0.55, 0.15) if both else (0.85, 0.1, 0.1),
                    linewidth=1.8 if both else 2.6, zorder=3)
        if path_pts:
            px, py, pz = zip(*path_pts)
            px = pz if H == 2 else px
            ax.plot(px, py, color=(1.0, 0.55, 0.0), linewidth=3.4, marker="o", markersize=5, zorder=5)
            ax.annotate("start", (px[0], py[0]), xytext=(px[0], py[0] - 0.55), ha="center", fontsize=9)
            ax.annotate("path end" + (" (goal reached)" if status == "complete" else " (partial: goal unreachable)"),
                        (px[-1], py[-1]), xytext=(px[-1] - 0.3, py[-1] + (0.45 if py[-1] > 1 else -0.55)), ha="right", fontsize=9)
        ax.set_xlim(hlim[0] - 0.5, hlim[1] + 0.5)
        ax.set_ylim(lim[2][0] - 0.9, lim[2][1] + 1.1)
        ax.set_xlabel(a.side_axis)
        ax.grid(alpha=0.25)
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)
    axes[0].set_ylabel("y (up)")
    legend = [plt.Line2D([], [], color=(0.35, 0.6, 0.95), lw=7, label=a.labels[0]),
              plt.Line2D([], [], color=(0.4, 0.75, 0.4), lw=7, label=a.labels[1]),
              plt.Line2D([], [], color=(0.15, 0.55, 0.15), lw=2, label="tile link in both builds"),
              plt.Line2D([], [], color=(0.85, 0.1, 0.1), lw=2.6, label="tile link only in this build"),
              plt.Line2D([], [], color=(1.0, 0.55, 0.0), lw=3, label="findPath + findStraightPath")]
    if not (left[2] or right[2]):
        legend = legend[:4]
    fig.legend(handles=legend, loc="lower center", ncol=len(legend), fontsize=9.5, frameon=False, bbox_to_anchor=(0.5, 0.05))
    if a.caption:
        fig.text(0.5, 0.015, a.caption, ha="center", fontsize=9, color=(0.3, 0.3, 0.3))
    plt.subplots_adjust(left=0.05, right=0.99, top=0.86, bottom=0.24, wspace=0.06)
    fig.savefig(a.out)
    print("wrote", a.out)
    raise SystemExit

fig = plt.figure(figsize=(14, 6.6), dpi=130)
fig.suptitle(a.title, fontsize=14, fontweight="bold", y=0.97)
for k, ((polys, portals, path_pts, status), sub, other) in enumerate(
        [(left, a.left_title, right_links), (right, a.right_title, left_links)]):
    ax = fig.add_subplot(1, 2, k + 1, projection="3d")
    ax.set_title(sub, fontsize=11.5, pad=2)
    ys_drawn = [v[1] for vs in polys.values() if inside(vs) for v in vs]
    ymin, ymax = min(ys_drawn), max(ys_drawn)
    faces, colors = [], []
    for key, vs in polys.items():
        if not inside(vs):
            continue
        h = sum(v[1] for v in vs) / len(vs)
        if a.floor_split is not None:
            col = (0.35, 0.6, 0.95, 0.45) if h < a.floor_split else (0.45, 0.8, 0.45, 0.45)
        else:
            t = (h - ymin) / max(ymax - ymin, 1e-6)
            col = (0.3 + 0.2 * t, 0.55 + 0.3 * t, 0.95 - 0.5 * t, 0.45)
        faces.append([(v[0], v[2], v[1]) for v in vs])
        colors.append(col)
    ax.add_collection3d(Poly3DCollection(faces, facecolors=colors, edgecolors=(0.1, 0.2, 0.3, 0.6), linewidths=0.6))

    # Tile border planes.
    for bx in a.border:
        ax.add_collection3d(Poly3DCollection(
            [[(bx, lim[1][0], lim[2][0] - 0.3), (bx, lim[1][1], lim[2][0] - 0.3),
              (bx, lim[1][1], lim[2][1] + 0.3), (bx, lim[1][0], lim[2][1] + 0.3)]],
            facecolors=(0.5, 0.5, 0.5, 0.10), edgecolors=(0.4, 0.4, 0.4, 0.5), linestyles="dashed", linewidths=0.8))

    # Links between tiles: polygon centre -> portal midpoint -> target centre.
    # A link is drawn once per polygon pair; red if only this build has it.
    seen = set()
    same, cross = [], []
    for frm, to, seg in portals:
        pair = tuple(sorted([frm, to]))
        if pair in seen or frm not in polys or to not in polys:
            continue
        if not (inside(polys[frm]) and inside(polys[to])):
            continue
        seen.add(pair)
        c0, c1 = center(polys[frm]), center(polys[to])
        mid = [(seg[i] + seg[i + 3]) / 2 for i in range(3)]
        line = [(c0[0], c0[2], c0[1]), (mid[0], mid[2], mid[1]), (c1[0], c1[2], c1[1])]
        (same if (frm, to) in other else cross).append(line)
    if same:
        ax.add_collection3d(Line3DCollection(same, colors=(0.15, 0.55, 0.15, 0.9), linewidths=1.6))
    if cross:
        ax.add_collection3d(Line3DCollection(cross, colors=(0.85, 0.1, 0.1, 0.95), linewidths=2.4))

    if path_pts:
        px, py, pz = zip(*path_pts)
        ax.plot(px, pz, py, color=(1.0, 0.55, 0.0), linewidth=3.2, marker="o", markersize=4, zorder=10)
        ax.scatter([px[0]], [pz[0]], [py[0]], color="black", s=30, zorder=11)
        ax.text(px[0], pz[0], py[0] + 0.25, "start", fontsize=9)
    ax.set_xlim(*lim[0])
    ax.set_ylim(*lim[1])
    ax.set_zlim(lim[2][0] - 0.3, lim[2][1] + 0.6)
    ax.set_box_aspect((lim[0][1] - lim[0][0], lim[1][1] - lim[1][0], max(lim[2][1] - lim[2][0] + 0.9, 1) * 1.6))
    ax.view_init(elev=a.view[0], azim=a.view[1])
    ax.set_xlabel("x")
    ax.set_ylabel("z")
    ax.set_zlabel("y (up)")
    ax.tick_params(labelsize=7)

legend = [plt.Line2D([], [], color=(0.15, 0.55, 0.15), lw=2, label="link in both builds"),
          plt.Line2D([], [], color=(0.85, 0.1, 0.1), lw=2.4, label="link only in this build")]
if left[2] or right[2]:
    legend.append(plt.Line2D([], [], color=(1.0, 0.55, 0.0), lw=3, label="path from findPath + findStraightPath"))
if a.border:
    legend.append(matplotlib.patches.Patch(facecolor=(0.5, 0.5, 0.5, 0.25), edgecolor=(0.4, 0.4, 0.4), label="tile border"))
fig.legend(handles=legend, loc="lower center", ncol=len(legend), fontsize=9.5, frameon=False, bbox_to_anchor=(0.5, 0.045))
if a.caption:
    fig.text(0.5, 0.012, a.caption, ha="center", fontsize=9, color=(0.3, 0.3, 0.3))
plt.subplots_adjust(left=0.0, right=1.0, top=0.9, bottom=0.1, wspace=0.0)
fig.savefig(a.out)
print("wrote", a.out)
