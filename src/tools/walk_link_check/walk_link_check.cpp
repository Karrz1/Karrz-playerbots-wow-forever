/*
 * walk_link_check - offline check of candidate walk links for the bots' travel graph.
 *
 * Reads the "[walk_gap_pair]" lines the worldserver writes at startup (Playerbot.log), loads the mmaps of each map named there, and runs
 * Detour findPath between the two places of every pair with the same filter the movement uses (ground, steep ground, road; no water). A pair
 * is a link only when the path is complete (not partial, ends on the target polygon) and has no cliff face (a drop of 15 y or more over no
 * more ground than the drop). The checked links are printed as a table, one line each with the path length.
 *
 * Usage: walk_link_check <mmaps_dir> <Playerbot.log> [links_out.txt]
 *
 * Pure Detour API, like mmap_probe: no TC core, no worldserver needed. The mmaps must be the ones the server runs on.
 */

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourCommon.h>
#include <DetourAlloc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

// Header layout copied verbatim from src/common/mmaps_common/MMapDefines.h.
// MUST stay byte-for-byte identical with the generator output.
// Header layout copied verbatim from src/common/mmaps_common/MMapDefines.h.
// MUST stay byte-for-byte identical with the generator output.
static constexpr uint32_t MMAP_MAGIC = 0x4d4d4150; // 'MMAP'
static constexpr uint32_t MMAP_VERSION_EXPECTED = 16;

#pragma pack(push, 1)
struct MmapNavMeshHeader
{
    uint32_t mmapMagic;
    uint32_t mmapVersion;
    dtNavMeshParams params;
    uint32_t offmeshConnectionCount;
};
struct MmapTileHeader
{
    uint32_t mmapMagic;
    uint32_t dtVersion;
    uint32_t mmapVersion;
    uint32_t size;
    char     usesLiquids;
    char     padding[3];
};
#pragma pack(pop)

static_assert(sizeof(MmapNavMeshHeader) == 40, "MmapNavMeshHeader size drift");
static_assert(sizeof(MmapTileHeader) == 20, "MmapTileHeader size drift");

// Convert a TC area id to the bitmask Detour's filter expects (matches the
// formula in MMapDefines.h NavTerrainFlag). The probe enables WALK on the
// usual surfaces (ground, ground steep, water, road). Off-mesh links and
// magma/slime intentionally left off — same as PathGenerator's default
// "land creature" filter.
static constexpr uint16_t NAV_GROUND       = 1u << (11 - 11); // 0x01
static constexpr uint16_t NAV_GROUND_STEEP = 1u << (11 - 10); // 0x02
static constexpr uint16_t NAV_WATER        = 1u << (11 - 9);  // 0x04
// NAV_MAGMA_SLIME = 0x08
static constexpr uint16_t NAV_ROAD         = 1u << (11 - 7);  // 0x10

namespace
{
std::string FormatMmapPath(std::string const& dir, uint32_t mapId)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u.mmap", mapId);
    return (std::filesystem::path(dir) / buf).string();
}

bool LoadNavMeshParams(std::string const& dir, uint32_t mapId, dtNavMeshParams& params)
{
    std::string path = FormatMmapPath(dir, mapId);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
    {
        std::fprintf(stderr, "ERROR: cannot open %s\n", path.c_str());
        return false;
    }
    MmapNavMeshHeader header{};
    if (std::fread(&header, sizeof(header), 1, f) != 1)
    {
        std::fprintf(stderr, "ERROR: short read of %s\n", path.c_str());
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    if (header.mmapMagic != MMAP_MAGIC)
    {
        std::fprintf(stderr, "ERROR: %s magic mismatch (got 0x%08x, want 0x%08x)\n",
            path.c_str(), header.mmapMagic, MMAP_MAGIC);
        return false;
    }
    if (header.mmapVersion != MMAP_VERSION_EXPECTED)
    {
        std::fprintf(stderr, "ERROR: %s version mismatch (got %u, want %u)\n",
            path.c_str(), header.mmapVersion, MMAP_VERSION_EXPECTED);
        return false;
    }
    params = header.params;
    return true;
}

struct LoadedTile
{
    int x;
    int y;
};

// Add one .mmtile file to the dtNavMesh. Returns true on success.
bool AddTileFromFile(dtNavMesh* navMesh, std::string const& path, int tx, int ty)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
    {
        std::fprintf(stderr, "WARN: cannot open %s\n", path.c_str());
        return false;
    }

    MmapTileHeader header{};
    if (std::fread(&header, sizeof(header), 1, f) != 1)
    {
        std::fprintf(stderr, "WARN: short header read %s\n", path.c_str());
        std::fclose(f);
        return false;
    }
    if (header.mmapMagic != MMAP_MAGIC || header.mmapVersion != MMAP_VERSION_EXPECTED)
    {
        std::fprintf(stderr, "WARN: bad header for %s (magic=0x%08x ver=%u)\n",
            path.c_str(), header.mmapMagic, header.mmapVersion);
        std::fclose(f);
        return false;
    }

    void* data = dtAlloc(header.size, DT_ALLOC_PERM);
    if (!data)
    {
        std::fclose(f);
        return false;
    }
    if (std::fread(data, header.size, 1, f) != 1)
    {
        std::fprintf(stderr, "WARN: short data read %s\n", path.c_str());
        dtFree(data);
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    dtTileRef tileRef = 0;
    dtStatus s = navMesh->addTile(static_cast<unsigned char*>(data), header.size, DT_TILE_FREE_DATA, 0, &tileRef);
    if (dtStatusFailed(s))
    {
        std::fprintf(stderr, "WARN: addTile failed for [%d,%d] status=0x%08x\n", tx, ty, s);
        dtFree(data);
        return false;
    }
    return true;
}

int LoadAllTiles(dtNavMesh* navMesh, std::string const& dir, uint32_t mapId, std::vector<LoadedTile>& loaded)
{
    // Pattern: 0001_XX_YY.mmtile
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%04u_", mapId);
    std::regex re(R"((\d{4})_(\d{2})_(\d{2})\.mmtile)");

    int n = 0;
    for (auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;
        std::string name = entry.path().filename().string();
        std::smatch m;
        if (!std::regex_match(name, m, re))
            continue;
        if (std::stoul(m[1].str()) != mapId)
            continue;
        int tx = std::stoi(m[2].str());
        int ty = std::stoi(m[3].str());
        if (AddTileFromFile(navMesh, entry.path().string(), tx, ty))
        {
            loaded.push_back({ tx, ty });
            ++n;
        }
    }
    return n;
}

} // namespace

namespace
{
constexpr int kMaxPolys = 4096;            // a 1,100 y walk can cross some hundreds of polygons
constexpr float kCliffDropYards = 15.0f;   // mirrors Fleet::kCliffDropYards (CityLayer.h)

struct Pair
{
    uint32_t map = 0;
    float ax = 0, ay = 0, az = 0;
    float bx = 0, by = 0, bz = 0;
    float d = 0;
};

bool Parse(std::string const& line, Pair& p)
{
    static std::regex const re(R"(\[walk_gap_pair\] map=(\d+) a=\d+ ax=(-?[\d.]+) ay=(-?[\d.]+) az=(-?[\d.]+) b=\d+ bx=(-?[\d.]+) by=(-?[\d.]+) bz=(-?[\d.]+) d=(\d+))");
    std::smatch m;
    if (!std::regex_search(line, m, re))
        return false;
    p.map = uint32_t(std::stoul(m[1]));
    p.ax = std::stof(m[2]); p.ay = std::stof(m[3]); p.az = std::stof(m[4]);
    p.bx = std::stof(m[5]); p.by = std::stof(m[6]); p.bz = std::stof(m[7]);
    p.d = std::stof(m[8]);
    return true;
}

struct Verdict
{
    char const* status = "";
    float length = 0.f;
    bool ok = false;
};

// TC (x, y, z) -> Detour (y, z, x).
Verdict Check(dtNavMeshQuery* query, Pair const& p)
{
    Verdict v;
    dtQueryFilter filter;
    filter.setIncludeFlags(NAV_GROUND | NAV_GROUND_STEEP | NAV_ROAD);
    filter.setExcludeFlags(0);
    float const ext[3] = { 4.0f, 12.0f, 4.0f };
    float a[3] = { p.ay, p.az, p.ax }, b[3] = { p.by, p.bz, p.bx };
    dtPolyRef ra = 0, rb = 0;
    float ca[3] = {}, cb[3] = {};
    query->findNearestPoly(a, ext, &filter, &ra, ca);
    query->findNearestPoly(b, ext, &filter, &rb, cb);
    if (!ra || !rb) { v.status = "NO_POLY"; return v; }
    static dtPolyRef polys[kMaxPolys];
    int n = 0;
    dtStatus s = query->findPath(ra, rb, ca, cb, &filter, polys, &n, kMaxPolys);
    if (dtStatusFailed(s) || n == 0) { v.status = "NOPATH"; return v; }
    if ((s & DT_PARTIAL_RESULT) || polys[n - 1] != rb) { v.status = "PARTIAL"; return v; }
    static float pts[kMaxPolys * 3];
    static unsigned char flags[kMaxPolys];
    static dtPolyRef refs[kMaxPolys];
    int np = 0;
    s = query->findStraightPath(ca, cb, polys, n, pts, flags, refs, &np, kMaxPolys, 0);
    if (dtStatusFailed(s) || np < 2) { v.status = "NOPATH"; return v; }
    float len = 0.f;
    bool cliff = false;
    for (int i = 1; i < np; ++i)
    {
        float const dx = pts[i * 3 + 2] - pts[(i - 1) * 3 + 2], dy = pts[i * 3 + 0] - pts[(i - 1) * 3 + 0];
        float const dz = pts[i * 3 + 1] - pts[(i - 1) * 3 + 1];
        float const run = std::hypot(dx, dy);
        len += std::sqrt(run * run + dz * dz);
        if (std::fabs(dz) >= kCliffDropYards && run <= std::fabs(dz))
            cliff = true;
    }
    v.length = len;
    if (cliff) { v.status = "CLIFF"; return v; }
    if (len > 3.0f * p.d + 300.0f) { v.status = "LONG"; return v; }
    v.status = "OK";
    v.ok = true;
    return v;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "Usage: %s <mmaps_dir> <Playerbot.log> [links_out.txt]\n", argv[0]);
        return 2;
    }
    std::string const dir = argv[1];
    std::ifstream in(argv[2]);
    if (!in)
    {
        std::fprintf(stderr, "ERROR: cannot open %s\n", argv[2]);
        return 2;
    }
    std::map<uint32_t, std::vector<Pair>> by_map;
    std::string line;
    size_t total = 0;
    while (std::getline(in, line))
    {
        Pair p;
        if (Parse(line, p))
        {
            by_map[p.map].push_back(p);
            ++total;
        }
    }
    if (!total)
    {
        std::fprintf(stderr, "ERROR: no [walk_gap_pair] lines in %s (start a worldserver build that writes them first)\n", argv[2]);
        return 2;
    }

    std::vector<std::string> table;
    for (auto& [map_id, pairs] : by_map)
    {
        dtNavMeshParams params{};
        if (!LoadNavMeshParams(dir, map_id, params))
            continue;
        dtNavMesh* mesh = dtAllocNavMesh();
        if (!mesh || dtStatusFailed(mesh->init(&params)))
        {
            std::fprintf(stderr, "ERROR: dtNavMesh init failed for map %u\n", map_id);
            continue;
        }
        std::vector<LoadedTile> loaded;
        int const tiles = LoadAllTiles(mesh, dir, map_id, loaded);
        std::printf("map %u: %d tile(s), %zu candidate pair(s)\n", map_id, tiles, pairs.size());
        dtNavMeshQuery* query = dtAllocNavMeshQuery();
        if (tiles == 0 || !query || dtStatusFailed(query->init(mesh, 65535)))
        {
            dtFreeNavMesh(mesh);
            continue;
        }
        for (Pair const& p : pairs)
        {
            Verdict const v = Check(query, p);
            std::printf("  %-8s map %u (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f) straight %.0f y, path %.0f y\n",
                        v.status, map_id, p.ax, p.ay, p.az, p.bx, p.by, p.bz, p.d, v.length);
            if (v.ok)
            {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%u %.1f %.1f %.1f %.1f %.1f %.1f %.0f", map_id, p.ax, p.ay, p.az, p.bx, p.by, p.bz, v.length);
                table.push_back(buf);
            }
        }
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
    }
    std::printf("\nLINK TABLE (map ax ay az bx by bz path_length), %zu of %zu pair(s) checked:\n", table.size(), total);
    for (std::string const& t : table)
        std::printf("%s\n", t.c_str());
    if (argc > 3)
    {
        std::ofstream out(argv[3]);
        for (std::string const& t : table)
            out << t << "\n";
    }
    return 0;
}
