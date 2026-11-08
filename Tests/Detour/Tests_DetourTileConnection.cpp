#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"

#include "catch2/catch_amalgamated.hpp"

#include <vector>

// Links between polygons of neighbouring tiles (dtNavMesh::connectExtLinks).
// Each test builds two 10x10 tiles side by side along x, so their shared border is
// the plane x = 10 and the portal edges run along z.
namespace
{
constexpr float cellSize = 1.0f;
constexpr float cellHeight = 0.1f;
constexpr int tileCells = 10;
constexpr float tileSize = tileCells * cellSize;
constexpr unsigned short meshNullIdx = 0xffff;
constexpr int nvp = 6;

struct Agent
{
	float height;
	float climb;
};

// A polygon as (x, y, z) world coordinates.
using Poly = std::vector<float>;

// A quad spanning [x0, x1] x [0, tileSize] whose height changes only along z:
// y0 at z = 0 and y1 at z = tileSize.
Poly quad(float x0, float x1, float y0, float y1)
{
	return {
		x0, y0, 0.0f,
		x0, y1, tileSize,
		x1, y1, tileSize,
		x1, y0, 0.0f,
	};
}

// Builds tile (tx, 0) from polygons given in world coordinates and adds it to the mesh.
// Polygon edges that lie on the tile border become portals, as in an rcPolyMesh.
void addTile(dtNavMesh& navMesh, int tx, const std::vector<Poly>& polys, const Agent& agent)
{
	const float bmin[3] = { tx * tileSize, 0.0f, 0.0f };
	const float bmax[3] = { (tx + 1) * tileSize, tileSize, tileSize };

	std::vector<unsigned short> verts;
	std::vector<unsigned short> polyData;
	for (const Poly& poly : polys)
	{
		const int nv = static_cast<int>(poly.size()) / 3;
		REQUIRE(nv <= nvp);
		const int first = static_cast<int>(verts.size()) / 3;
		for (int i = 0; i < nv; ++i)
		{
			verts.push_back(static_cast<unsigned short>(roundf((poly[i * 3 + 0] - bmin[0]) / cellSize)));
			verts.push_back(static_cast<unsigned short>(roundf((poly[i * 3 + 1] - bmin[1]) / cellHeight)));
			verts.push_back(static_cast<unsigned short>(roundf((poly[i * 3 + 2] - bmin[2]) / cellSize)));
		}
		std::vector<unsigned short> p(nvp * 2, meshNullIdx);
		for (int i = 0; i < nv; ++i)
		{
			p[i] = static_cast<unsigned short>(first + i);
			const unsigned short* va = &verts[(first + i) * 3];
			const unsigned short* vb = &verts[(first + (i + 1) % nv) * 3];
			if (va[0] == 0 && vb[0] == 0)
				p[nvp + i] = 0x8000 | 0; // Portal x-
			else if (va[2] == tileCells && vb[2] == tileCells)
				p[nvp + i] = 0x8000 | 1; // Portal z+
			else if (va[0] == tileCells && vb[0] == tileCells)
				p[nvp + i] = 0x8000 | 2; // Portal x+
			else if (va[2] == 0 && vb[2] == 0)
				p[nvp + i] = 0x8000 | 3; // Portal z-
		}
		polyData.insert(polyData.end(), p.begin(), p.end());
	}

	const int polyCount = static_cast<int>(polys.size());
	std::vector<unsigned short> polyFlags(polyCount, 1);
	std::vector<unsigned char> polyAreas(polyCount, 0);

	dtNavMeshCreateParams params;
	memset(&params, 0, sizeof(params));
	params.verts = verts.data();
	params.vertCount = static_cast<int>(verts.size()) / 3;
	params.polys = polyData.data();
	params.polyFlags = polyFlags.data();
	params.polyAreas = polyAreas.data();
	params.polyCount = polyCount;
	params.nvp = nvp;
	params.tileX = tx;
	params.tileY = 0;
	params.tileLayer = 0;
	dtVcopy(params.bmin, bmin);
	dtVcopy(params.bmax, bmax);
	params.walkableHeight = agent.height;
	params.walkableRadius = 0.5f;
	params.walkableClimb = agent.climb;
	params.cs = cellSize;
	params.ch = cellHeight;
	params.buildBvTree = true;

	unsigned char* data = nullptr;
	int dataSize = 0;
	REQUIRE(dtCreateNavMeshData(&params, &data, &dataSize));
	REQUIRE(dtStatusSucceed(navMesh.addTile(data, dataSize, DT_TILE_FREE_DATA, 0, nullptr)));
}

void initNavMesh(dtNavMesh& navMesh)
{
	dtNavMeshParams params;
	memset(&params, 0, sizeof(params));
	params.tileWidth = tileSize;
	params.tileHeight = tileSize;
	params.maxTiles = 4;
	params.maxPolys = 16;
	REQUIRE(dtStatusSucceed(navMesh.init(&params)));
}

struct TileLink
{
	int fromPoly;
	int toPoly;
	float zmin; // Portal extent along the border. [Unit: wu]
	float zmax;
};

// Returns the links from polygons in tile (fromTx, 0) to polygons in tile (toTx, 0).
std::vector<TileLink> tileLinks(const dtNavMesh& navMesh, int fromTx, int toTx)
{
	const dtMeshTile* from = navMesh.getTileAt(fromTx, 0, 0);
	const dtMeshTile* to = navMesh.getTileAt(toTx, 0, 0);
	REQUIRE(from);
	REQUIRE(to);

	std::vector<TileLink> links;
	for (int i = 0; i < from->header->polyCount; ++i)
	{
		const dtPoly& poly = from->polys[i];
		for (unsigned int k = poly.firstLink; k != DT_NULL_LINK; k = from->links[k].next)
		{
			const dtLink& link = from->links[k];
			const dtMeshTile* targetTile = nullptr;
			const dtPoly* targetPoly = nullptr;
			REQUIRE(dtStatusSucceed(navMesh.getTileAndPolyByRef(link.ref, &targetTile, &targetPoly)));
			if (targetTile != to)
				continue;

			const float* va = &from->verts[poly.verts[link.edge] * 3];
			const float* vb = &from->verts[poly.verts[(link.edge + 1) % poly.vertCount] * 3];
			const float z0 = va[2] + (vb[2] - va[2]) * (link.bmin / 255.0f);
			const float z1 = va[2] + (vb[2] - va[2]) * (link.bmax / 255.0f);
			links.push_back({ i, static_cast<int>(targetPoly - to->polys), dtMin(z0, z1), dtMax(z0, z1) });
		}
	}
	return links;
}

// One portal step is 1/255 of the edge, so allow that much rounding.
constexpr float portalEps = tileSize / 255.0f;
}

TEST_CASE("Tile links between stacked floors", "[detour][tileconnection]")
{
	// Two floors, one above the other, crossing the border between tile 0 and tile 1.
	// Each floor should only link to the same floor in the other tile.
	const auto linksPerFloor = [](const Agent& agent, float upperY, int expectedLinks) {
		dtNavMesh navMesh;
		initNavMesh(navMesh);
		addTile(navMesh, 0, { quad(0, 10, 0, 0), quad(0, 10, upperY, upperY) }, agent);
		addTile(navMesh, 1, { quad(10, 20, 0, 0), quad(10, 20, upperY, upperY) }, agent);

		for (int tx = 0; tx < 2; ++tx)
		{
			const std::vector<TileLink> links = tileLinks(navMesh, tx, 1 - tx);
			CHECK(links.size() == static_cast<size_t>(expectedLinks));
			int sameFloor = 0;
			for (const TileLink& link : links)
				sameFloor += link.fromPoly == link.toPoly;
			CHECK(sameFloor == 2);
		}
	};

	SECTION("Floors closer than twice the climb, with room for an agent between them, are not cross-linked")
	{
		// Issue 721: walkableClimb = 1, walkableHeight = 1.5, floors 2 apart. The lower floor is walkable
		// (2 >= 1.5), and 2 <= 2 * walkableClimb.
		linksPerFloor({ 1.5f, 1.0f }, 2.0f, 2);
	}

	SECTION("Floors exactly the agent height apart are not cross-linked")
	{
		linksPerFloor({ 2.0f, 1.0f }, 2.0f, 2);
	}

	SECTION("Floors farther apart than twice the climb are not cross-linked")
	{
		linksPerFloor({ 1.5f, 1.0f }, 2.5f, 2);
	}

	SECTION("Without an agent height, floors within twice the climb are cross-linked as before")
	{
		// Tiles built without walkableHeight keep the old rule.
		linksPerFloor({ 0.0f, 1.0f }, 2.0f, 4);
	}
}

TEST_CASE("Tile links between edges of the same floor", "[detour][tileconnection]")
{
	// Tiles may rasterize the same surface slightly differently, so edges up to
	// 2 * walkableClimb apart still link along their whole length.
	const auto checkFullLink = [](float y0, float y1) {
		const Agent agent = { 2.0f, 1.0f };
		dtNavMesh navMesh;
		initNavMesh(navMesh);
		addTile(navMesh, 0, { quad(0, 10, 0, 0) }, agent);
		addTile(navMesh, 1, { quad(10, 20, y0, y1) }, agent);

		for (int tx = 0; tx < 2; ++tx)
		{
			const std::vector<TileLink> links = tileLinks(navMesh, tx, 1 - tx);
			CHECK(links.size() == 1);
			if (links.size() != 1)
				continue;
			CHECK(links[0].zmin == Catch::Approx(0.0f).margin(portalEps));
			CHECK(links[0].zmax == Catch::Approx(tileSize).margin(portalEps));
		}
	};

	SECTION("Small mismatch")
	{
		checkFullLink(0.3f, 0.1f);
	}

	SECTION("Mismatch up to twice the climb")
	{
		checkFullLink(1.5f, 2.0f);
	}

	SECTION("Mismatch reaching the agent height at one end only")
	{
		// Only edges at least walkableHeight apart along the whole overlap are separate floors.
		checkFullLink(1.0f, 2.5f);
	}
}
