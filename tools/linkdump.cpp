// Builds a tiled navmesh from an OBJ file the way RecastDemo's Sample_TileMesh does (watershed, default
// filters), then prints every link between tiles and the results of a fixed set of path queries.
// Link it once against stock Detour and once against the patched Detour and diff the outputs.
//
// usage: linkdump mesh.obj tileSize agentHeight agentRadius agentClimb queries > out.txt
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "Recast.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::vector<float> verts;
static std::vector<int> tris;

static bool loadObj(const char* path)
{
	std::ifstream in(path);
	if (!in)
		return false;
	std::string line;
	while (std::getline(in, line))
	{
		std::istringstream s(line);
		std::string tag;
		s >> tag;
		if (tag == "v")
		{
			float x, y, z;
			s >> x >> y >> z;
			verts.insert(verts.end(), { x, y, z });
		}
		else if (tag == "f")
		{
			std::vector<int> idx;
			std::string tok;
			while (s >> tok)
			{
				int i = atoi(tok.c_str());
				idx.push_back(i < 0 ? static_cast<int>(verts.size() / 3) + i : i - 1);
			}
			for (size_t k = 2; k < idx.size(); ++k)
				tris.insert(tris.end(), { idx[0], idx[k - 1], idx[k] });
		}
	}
	return true;
}

static unsigned int rng = 12345;
static float frand()
{
	rng = rng * 1664525u + 1013904223u;
	return (rng >> 8) / 16777216.0f;
}

int main(int argc, char** argv)
{
	if (argc < 7)
	{
		fprintf(stderr, "usage: %s mesh.obj tileSize agentHeight agentRadius agentClimb queries\n", argv[0]);
		return 2;
	}
	if (!loadObj(argv[1]))
		return 1;
	const int tileSize = atoi(argv[2]);
	const float agentHeight = static_cast<float>(atof(argv[3]));
	const float agentRadius = static_cast<float>(atof(argv[4]));
	const float agentClimb = static_cast<float>(atof(argv[5]));
	const int queries = atoi(argv[6]);

	const int nverts = static_cast<int>(verts.size() / 3);
	const int ntris = static_cast<int>(tris.size() / 3);
	float bmin[3], bmax[3];
	rcCalcBounds(verts.data(), nverts, bmin, bmax);

	rcConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.cs = 0.3f;
	cfg.ch = 0.2f;
	cfg.walkableSlopeAngle = 45.0f;
	cfg.walkableHeight = static_cast<int>(ceilf(agentHeight / cfg.ch));
	cfg.walkableClimb = static_cast<int>(floorf(agentClimb / cfg.ch));
	cfg.walkableRadius = static_cast<int>(ceilf(agentRadius / cfg.cs));
	cfg.maxEdgeLen = static_cast<int>(12.0f / cfg.cs);
	cfg.maxSimplificationError = 1.3f;
	cfg.minRegionArea = 8 * 8;
	cfg.mergeRegionArea = 20 * 20;
	cfg.maxVertsPerPoly = 6;
	cfg.tileSize = tileSize;
	cfg.borderSize = cfg.walkableRadius + 3;
	cfg.width = cfg.tileSize + cfg.borderSize * 2;
	cfg.height = cfg.tileSize + cfg.borderSize * 2;
	cfg.detailSampleDist = cfg.cs * 6.0f;
	cfg.detailSampleMaxError = cfg.ch * 1.0f;

	int gw = 0, gh = 0;
	rcCalcGridSize(bmin, bmax, cfg.cs, &gw, &gh);
	const int tw = (gw + tileSize - 1) / tileSize;
	const int th = (gh + tileSize - 1) / tileSize;
	const float tcs = tileSize * cfg.cs;

	dtNavMeshParams nmp;
	memset(&nmp, 0, sizeof(nmp));
	dtVcopy(nmp.orig, bmin);
	nmp.tileWidth = tcs;
	nmp.tileHeight = tcs;
	nmp.maxTiles = 1 << 12;
	nmp.maxPolys = 1 << 10;
	dtNavMesh* nav = dtAllocNavMesh();
	if (dtStatusFailed(nav->init(&nmp)))
		return 1;

	rcContext ctx(false);
	std::vector<unsigned char> areas(ntris);
	for (int ty = 0; ty < th; ++ty)
	{
		for (int tx = 0; tx < tw; ++tx)
		{
			rcConfig tc = cfg;
			tc.bmin[0] = bmin[0] + tx * tcs - cfg.borderSize * cfg.cs;
			tc.bmin[1] = bmin[1];
			tc.bmin[2] = bmin[2] + ty * tcs - cfg.borderSize * cfg.cs;
			tc.bmax[0] = bmin[0] + (tx + 1) * tcs + cfg.borderSize * cfg.cs;
			tc.bmax[1] = bmax[1];
			tc.bmax[2] = bmin[2] + (ty + 1) * tcs + cfg.borderSize * cfg.cs;

			rcHeightfield* hf = rcAllocHeightfield();
			rcCreateHeightfield(&ctx, *hf, tc.width, tc.height, tc.bmin, tc.bmax, tc.cs, tc.ch);
			memset(areas.data(), 0, areas.size());
			rcMarkWalkableTriangles(&ctx, tc.walkableSlopeAngle, verts.data(), nverts, tris.data(), ntris, areas.data());
			rcRasterizeTriangles(&ctx, verts.data(), nverts, tris.data(), areas.data(), ntris, *hf, tc.walkableClimb);
			rcFilterLowHangingWalkableObstacles(&ctx, tc.walkableClimb, *hf);
			rcFilterLedgeSpans(&ctx, tc.walkableHeight, tc.walkableClimb, *hf);
			rcFilterWalkableLowHeightSpans(&ctx, tc.walkableHeight, *hf);

			rcCompactHeightfield* chf = rcAllocCompactHeightfield();
			rcBuildCompactHeightfield(&ctx, tc.walkableHeight, tc.walkableClimb, *hf, *chf);
			rcFreeHeightField(hf);
			rcErodeWalkableArea(&ctx, tc.walkableRadius, *chf);
			rcBuildDistanceField(&ctx, *chf);
			rcBuildRegions(&ctx, *chf, tc.borderSize, tc.minRegionArea, tc.mergeRegionArea);

			rcContourSet* cset = rcAllocContourSet();
			rcBuildContours(&ctx, *chf, tc.maxSimplificationError, tc.maxEdgeLen, *cset);
			rcPolyMesh* pmesh = rcAllocPolyMesh();
			rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
			bool ok = cset->nconts > 0 && rcBuildPolyMesh(&ctx, *cset, tc.maxVertsPerPoly, *pmesh) && pmesh->npolys > 0 &&
					  rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, tc.detailSampleDist, tc.detailSampleMaxError, *dmesh);
			rcFreeCompactHeightfield(chf);
			rcFreeContourSet(cset);
			if (ok)
			{
				for (int i = 0; i < pmesh->npolys; ++i)
					pmesh->flags[i] = pmesh->areas[i] == RC_WALKABLE_AREA ? 1 : 0;

				dtNavMeshCreateParams p;
				memset(&p, 0, sizeof(p));
				p.verts = pmesh->verts;
				p.vertCount = pmesh->nverts;
				p.polys = pmesh->polys;
				p.polyAreas = pmesh->areas;
				p.polyFlags = pmesh->flags;
				p.polyCount = pmesh->npolys;
				p.nvp = pmesh->nvp;
				p.detailMeshes = dmesh->meshes;
				p.detailVerts = dmesh->verts;
				p.detailVertsCount = dmesh->nverts;
				p.detailTris = dmesh->tris;
				p.detailTriCount = dmesh->ntris;
				p.walkableHeight = agentHeight;
				p.walkableRadius = agentRadius;
				p.walkableClimb = agentClimb;
				p.tileX = tx;
				p.tileY = ty;
				p.tileLayer = 0;
				dtVcopy(p.bmin, pmesh->bmin);
				dtVcopy(p.bmax, pmesh->bmax);
				p.cs = tc.cs;
				p.ch = tc.ch;
				p.buildBvTree = true;
				unsigned char* data = 0;
				int dataSize = 0;
				if (dtCreateNavMeshData(&p, &data, &dataSize))
					nav->addTile(data, dataSize, DT_TILE_FREE_DATA, 0, 0);
			}
			rcFreePolyMesh(pmesh);
			rcFreePolyMeshDetail(dmesh);
		}
	}

	// Every link to a polygon in another tile.
	const dtNavMesh* cnav = nav;
	int nlinks = 0, ntiles = 0, npolys = 0;
	for (int t = 0; t < cnav->getMaxTiles(); ++t)
	{
		const dtMeshTile* tile = cnav->getTile(t);
		if (!tile->header)
			continue;
		ntiles++;
		npolys += tile->header->polyCount;
		for (int i = 0; i < tile->header->polyCount; ++i)
		{
			const dtPoly& poly = tile->polys[i];
			if (getenv("DUMP_GEOM") && poly.getType() == DT_POLYTYPE_GROUND)
			{
				printf("poly %d,%d p%d", tile->header->x, tile->header->y, i);
				for (int j = 0; j < poly.vertCount; ++j)
				{
					const float* v = &tile->verts[poly.verts[j] * 3];
					printf(" %.3f %.3f %.3f", v[0], v[1], v[2]);
				}
				printf("\n");
			}
			for (unsigned int k = poly.firstLink; k != DT_NULL_LINK; k = tile->links[k].next)
			{
				const dtLink& l = tile->links[k];
				const dtMeshTile* tt = 0;
				const dtPoly* tp = 0;
				cnav->getTileAndPolyByRef(l.ref, &tt, &tp);
				if (tt == tile)
					continue;
				nlinks++;
				printf("link %d,%d p%d e%d -> %d,%d p%d side%d portal %d-%d\n", tile->header->x, tile->header->y, i, l.edge,
					   tt->header->x, tt->header->y, static_cast<int>(tp - tt->polys), l.side, l.bmin, l.bmax);
				if (getenv("DUMP_GEOM"))
				{
					const float* va = &tile->verts[poly.verts[l.edge] * 3];
					const float* vb = &tile->verts[poly.verts[(l.edge + 1) % poly.vertCount] * 3];
					float p0[3], p1[3];
					dtVlerp(p0, va, vb, l.bmin / 255.0f);
					dtVlerp(p1, va, vb, l.bmax / 255.0f);
					printf("portal %d,%d p%d -> %d,%d p%d %.3f %.3f %.3f %.3f %.3f %.3f\n", tile->header->x, tile->header->y, i,
						   tt->header->x, tt->header->y, static_cast<int>(tp - tt->polys), p0[0], p0[1], p0[2], p1[0], p1[1], p1[2]);
				}
				if (getenv("DUMP_EDGES"))
				{
					const float* va = &tile->verts[poly.verts[l.edge] * 3];
					const float* vb = &tile->verts[poly.verts[(l.edge + 1) % poly.vertCount] * 3];
					printf("   from edge (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n", va[0], va[1], va[2], vb[0], vb[1], vb[2]);
					for (int j = 0; j < tp->vertCount; ++j)
					{
						if ((tp->neis[j] & DT_EXT_LINK) == 0)
							continue;
						const float* vc = &tt->verts[tp->verts[j] * 3];
						const float* vd = &tt->verts[tp->verts[(j + 1) % tp->vertCount] * 3];
						printf("   to portal edge side%d (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n", tp->neis[j] & 0xff, vc[0], vc[1], vc[2], vd[0], vd[1], vd[2]);
					}
				}
			}
		}
	}

	// A fixed set of path queries. The random points depend only on the polygons, which the patch
	// does not change, so both builds query the same pairs.
	dtNavMeshQuery* q = dtAllocNavMeshQuery();
	q->init(nav, 4096);
	dtQueryFilter filter;
	if (const char* qs = getenv("QUERY"))
	{
		float spos[3], epos[3];
		sscanf(qs, "%f %f %f %f %f %f", &spos[0], &spos[1], &spos[2], &epos[0], &epos[1], &epos[2]);
		const float ext[3] = { 1.0f, 0.5f, 1.0f };
		dtPolyRef sref = 0, eref = 0;
		float sn[3], en[3];
		q->findNearestPoly(spos, ext, &filter, &sref, sn);
		q->findNearestPoly(epos, ext, &filter, &eref, en);
		dtPolyRef path[512];
		int npath = 0;
		const dtStatus st = q->findPath(sref, eref, sn, en, &filter, path, &npath, 512);
		float straight[512 * 3];
		int nstraight = 0;
		if (npath > 0)
		{
			float target[3];
			dtVcopy(target, en);
			if (path[npath - 1] != eref)
				q->closestPointOnPoly(path[npath - 1], en, target, 0);
			q->findStraightPath(sn, target, path, npath, straight, 0, 0, &nstraight, 512, DT_STRAIGHTPATH_ALL_CROSSINGS);
		}
		printf("path %s", (st & DT_PARTIAL_RESULT) || npath == 0 || path[npath - 1] != eref ? "partial" : "complete");
		for (int k = 0; k < nstraight; ++k)
			printf(" %.3f %.3f %.3f", straight[k * 3], straight[k * 3 + 1], straight[k * 3 + 2]);
		printf("\n");
	}
	int nfound = 0;
	for (int n = 0; n < queries; ++n)
	{
		dtPolyRef sref, eref;
		float spos[3], epos[3];
		q->findRandomPoint(&filter, frand, &sref, spos);
		q->findRandomPoint(&filter, frand, &eref, epos);
		dtPolyRef path[512];
		int npath = 0;
		q->findPath(sref, eref, spos, epos, &filter, path, &npath, 512);
		float straight[512 * 3];
		int nstraight = 0;
		float len = 0;
		float pend[3] = { 0, 0, 0 };
		if (npath > 0)
		{
			q->findStraightPath(spos, epos, path, npath, straight, 0, 0, &nstraight, 512);
			for (int k = 1; k < nstraight; ++k)
				len += dtVdist(&straight[(k - 1) * 3], &straight[k * 3]);
			if (nstraight > 0)
				dtVcopy(pend, &straight[(nstraight - 1) * 3]);
		}
		const bool reached = npath > 0 && path[npath - 1] == eref;
		nfound += reached;
		printf("query %d polys %d reached %d len %.3f end %.2f %.2f %.2f\n", n, npath, reached ? 1 : 0, len, pend[0], pend[1],
			   pend[2]);
	}
	fprintf(stderr, "%s tile=%d h=%.2f r=%.2f climb=%.2f: %d tiles, %d polys, %d tile links, %d/%d queries reached\n", argv[1],
			tileSize, agentHeight, agentRadius, agentClimb, ntiles, npolys, nlinks, nfound, queries);
	dtFreeNavMeshQuery(q);
	dtFreeNavMesh(nav);
	return 0;
}
