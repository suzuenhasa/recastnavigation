# Tile links between stacked floors: test record

Scripts, scenes, images and results for the Detour fix "Don't link tile edges an agent can stand between". The fix is on branch `fix/tile-link-floors` of this fork, against recastnavigation `main` at 9f4ce64.

- `tools/linkdump.cpp`: builds a tiled navmesh from an OBJ file the way RecastDemo's Sample_TileMesh does (watershed, default filters). It prints every link between tiles and the results of a fixed set of random path queries. Build it once against `main` and once against the fix, then diff the two outputs. With `DUMP_GEOM=1` it also prints the polygons and portals. With `QUERY="sx sy sz ex ey ez"` it runs one path query (`findPath` + `findStraightPath`).
- `tools/render_links.py`: draws two `DUMP_GEOM` outputs side by side (matplotlib).
- `tools/make_images.sh`: regenerates `images/`.
- `scenes/`: `stacked_floors.obj` (the issue 721 setup) and `ramp_over_ground.obj`.
- `results/sweep_summary.txt`: 72 builds of the RecastDemo meshes (dungeon, nav_test, undulating) at tile sizes 8 to 64 and climbs 0.6 to 1.5.
- `results/scenes_summary.txt`: the two scenes.

## Reproduce

```sh
git clone https://github.com/recastnavigation/recastnavigation main && git -C main checkout 9f4ce64
git clone -b fix/tile-link-floors https://github.com/suzuenhasa/recastnavigation fix
for d in main fix; do
  cmake -S $d -B $d/build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRECASTNAVIGATION_DEMO=OFF -DRECASTNAVIGATION_EXAMPLES=OFF -DRECASTNAVIGATION_TESTS=OFF
  cmake --build $d/build
done
mkdir -p tools/bin
for d in main fix; do
  g++ -O2 -std=c++17 -Imain/Recast/Include -Imain/Detour/Include tools/linkdump.cpp \
    $d/build/Detour/libDetour.a $d/build/Recast/libRecast.a -o tools/bin/linkdump-$d
done

# One build, e.g. undulating.obj, tile size 16, agent height 2.0, radius 0.6, climb 1.5, 300 queries:
for d in main fix; do tools/bin/linkdump-$d main/RecastDemo/Bin/Meshes/undulating.obj 16 2.0 0.6 1.5 300 > $d.txt; done
diff main.txt fix.txt

# The images:
RECAST=main tools/make_images.sh
```

The unit tests are in the fix branch: `Tests/Detour/Tests_DetourTileConnection.cpp`, run with `./build/Tests/Tests "[tileconnection]"`.

Setup used: Ubuntu 22.04 (WSL2), GCC 11.4, CMake 4.4, Python 3 with matplotlib 3.10.
