#!/bin/sh
# Regenerates images/*.png. Needs tools/bin/linkdump-main and tools/bin/linkdump-fix (see README.md),
# RECAST pointing at a recastnavigation checkout (for undulating.obj), and matplotlib.
set -e
cd "$(dirname "$0")/.."
RECAST=$(cd "${RECAST:?set RECAST to a recastnavigation checkout}" && pwd)
mkdir -p images/geom
for v in main fix; do
	DUMP_GEOM=1 QUERY="3 0 5 16 2 5" tools/bin/linkdump-$v scenes/stacked_floors.obj 32 1.5 0.3 1.0 0 > images/geom/stacked.$v.geom
	DUMP_GEOM=1 tools/bin/linkdump-$v "$RECAST/RecastDemo/Bin/Meshes/undulating.obj" 16 2.0 0.6 1.5 0 > images/geom/und16.$v.geom
done
python3 tools/render_links.py images/stacked_floors_side.png "Two floors 2 apart across a tile border (issue 721 setup), side view" \
	images/geom/stacked.main.geom "main: each floor also links to the other floor" images/geom/stacked.fix.geom "This PR: each floor links only to itself" \
	--side --border 9.6 --floor-split 1.0 \
	--caption "walkableClimb 1.0, walkableHeight 1.5, cell 0.3 / 0.2, tile size 32. Path query from (3, 0, 5) on the lower floor to (16, 2, 5) on the upper floor; nothing connects the floors."
python3 tools/render_links.py images/undulating_overhang_side.png "RecastDemo undulating.obj at walkableClimb 1.5: an overhang across a tile border, side view" \
	images/geom/und16.main.geom "main: each layer also links to the other layer" images/geom/und16.fix.geom "This PR: each layer links only to itself" \
	--side --side-axis z --box 5004.8 5007.5 5008.5 5011.0 --border 5009.152 --floor-split -0.4 --labels "lower layer" "upper layer (overhang)" \
	--caption "Sample_TileMesh settings: cell 0.3 / 0.2, agent height 2.0, radius 0.6, climb 1.5, tile size 16. Polygons at x 5005-5007.5, seen along x. The layers are about 3 apart: both walkable (>= 2.0) and within 2 x 1.5."
