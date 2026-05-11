#pragma once

/** @file
 * @brief GLTF 2.0 exporter for pelpaint::ir::RegionGraph.
 */

#include "RegionGraph.hpp"    // pelpaint::ir::{RegionGraph, Region, Mesh, …}
#include "MeshBuilder.hpp"    // MeshBuilder::Build, NormaliseContour
#include "RDP.hpp"            // RDP::SimplifyPolygon

#include <algorithm>          // std::min, std::max
#include <cstdint>            // uint8_t, uint32_t
#include <cstring>            // std::memcpy
#include <fstream>            // std::ofstream
#include <iomanip>            // std::fixed, std::setprecision
#include <limits>             // std::numeric_limits
#include <sstream>            // std::ostringstream
#include <string>             // std::string
#include <vector>             // std::vector

namespace pelpaint::exporter {

struct GltfExportOptions {
    float depth      = 1.0f;   ///< extrusion depth in world units
    bool  symmetric  = false;  ///< true → ±depth/2; false → 0 to depth
    bool  normalise  = true;   ///< divide pixel coords by max(canvasW, canvasH)
    float rdpEpsilon = 0.5f;   ///< RDP simplification; 0 = collinear-only
};

/**
 * @brief GLTF 2.0 exporter for pelpaint::ir::RegionGraph.
 *
 * @details Exports a RegionGraph as a GLTF 2.0 scene producing
 * a .gltf (hand-crafted JSON) and a .bin (binary buffer sidecar).
 *
 * GLTF structure:
 * - One mesh      per valid region   (extruded 3-D slab via MeshBuilder)
 * - One material  per valid region   (flat PBR colour from Region::color)
 * - One node      per mesh           (flat scene graph, no hierarchy)
 * - Three bufferViews per region     positions | normals | indices
 * - Three accessors   per region     POSITION  | NORMAL  | indices
 * - One binary buffer                (all packed sequentially, 4-byte aligned)
 *
 * Accessor component types:
 * - POSITION / NORMAL : VEC3 FLOAT         (componentType 5126)
 * - indices           : SCALAR UNSIGNED_INT (componentType 5125)
 *
 * The POSITION accessor always carries "min" and "max" arrays as
 * required by the GLTF 2.0 specification.
 */
class GltfExporter {
public:
    /// Export `graph` to <basePath>.gltf and <basePath>.bin.
    ///
    /// If `basePath` already ends with ".gltf" the extension is stripped
    /// before adding it back, so passing either "output" or "output.gltf"
    /// produces the same result.
    ///
    /// @return true on success; false if no valid regions exist or
    ///         either output file cannot be opened for writing.
    ///
    /// @note All float data and all uint32 index data are inherently 4-byte
    ///       aligned: N*12 (positions) + N*12 (normals) + M*4 (indices) are
    ///       all exact multiples of 4, so no padding bytes are ever inserted.
    [[nodiscard]]
    static bool Save(
        const std::string&               basePath,
        const pelpaint::ir::RegionGraph& graph,
        const GltfExportOptions&          opts = {})
    {
        // Derive output file paths.
        std::string base = basePath;
        {
            constexpr std::string_view kExt = ".gltf";
            if (base.size() > kExt.size() &&
                base.compare(base.size() - kExt.size(), kExt.size(), kExt) == 0)
                base.resize(base.size() - kExt.size());
        }
        const std::string gltfPath = base + ".gltf";
        const std::string binPath  = base + ".bin";

        // Relative URI embedded in JSON (filename only, no directory)
        std::string binName = binPath;
        if (const auto slash = binPath.rfind('/'); slash != std::string::npos)
            binName = binPath.substr(slash + 1u);

        // Shared build context.
        const float canvasMaxDim = static_cast<float>(
            std::max(graph.canvasW, graph.canvasH));

        pelpaint::ir::ExtrudeOptions extOpts;
        extOpts.depth     = opts.depth;
        extOpts.symmetric = opts.symmetric;

        // Internal metadata types.
        struct BufView {
            uint32_t byteOffset;
            uint32_t byteLength;
            uint32_t target;   // 34962 = ARRAY_BUFFER, 34963 = ELEMENT_ARRAY_BUFFER
        };
        struct Accessor {
            uint32_t    bufViewIdx;
            uint32_t    componentType;  // 5126 = FLOAT, 5125 = UNSIGNED_INT
            uint32_t    count;
            const char* type;           // "VEC3" or "SCALAR" (string literals)
            bool        hasMinMax = false;
            float       mn[3]{};
            float       mx[3]{};
        };
        struct MeshEntry {
            int      regionId;
            float    color[3];  // region colour in [0, 255] per channel
            uint32_t accPos;    // accessor index for POSITION
            uint32_t accNrm;    // accessor index for NORMAL
            uint32_t accIdx;    // accessor index for indices
        };

        // Output containers.
        std::vector<uint8_t>   binBuf;
        std::vector<BufView>   bufViews;
        std::vector<Accessor>  accessors;
        std::vector<MeshEntry> meshEntries;

        binBuf.reserve(1u << 20u);  // 1 MiB initial reservation

        // Build one mesh per valid region.
        for (const auto& region : graph.regions) {
            if (!region.valid()) continue;

            // Optionally simplify the contour (RDP pass)
            pelpaint::ir::Region work = region;
            if (opts.rdpEpsilon > 0.f) {
                work.contour = pelpaint::ir::RDP::SimplifyPolygon(
                    region.contour, opts.rdpEpsilon);
                if (work.contour.size() < 3) continue;
            }

            // Optionally normalise pixel coords to [0, 1]
            if (opts.normalise && canvasMaxDim >= 1.f)
                work.contour = pelpaint::ir::MeshBuilder::NormaliseContour(
                    work.contour, canvasMaxDim);

            const pelpaint::ir::Mesh mesh =
                pelpaint::ir::MeshBuilder::Build(work, extOpts);
            if (mesh.empty()) continue;

            const auto numV = static_cast<uint32_t>(mesh.vertices.size());
            const auto numI = static_cast<uint32_t>(mesh.indices.size());

            // Positions (VEC3 FLOAT, ARRAY_BUFFER).
            {
                const auto byteOff = static_cast<uint32_t>(binBuf.size());

                float mn[3] = { std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max() };
                float mx[3] = { std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest() };

                for (const auto& v : mesh.vertices) {
                    appendFloat(binBuf, v.pos.x);
                    appendFloat(binBuf, v.pos.y);
                    appendFloat(binBuf, v.pos.z);
                    mn[0] = std::min(mn[0], v.pos.x); mx[0] = std::max(mx[0], v.pos.x);
                    mn[1] = std::min(mn[1], v.pos.y); mx[1] = std::max(mx[1], v.pos.y);
                    mn[2] = std::min(mn[2], v.pos.z); mx[2] = std::max(mx[2], v.pos.z);
                }
                const auto byteLen = numV * 3u * 4u;

                const auto bvIdx = static_cast<uint32_t>(bufViews.size());
                bufViews.push_back({ byteOff, byteLen, 34962u });

                const auto accPosIdx = static_cast<uint32_t>(accessors.size());
                Accessor a;
                a.bufViewIdx    = bvIdx;
                a.componentType = 5126u;
                a.count         = numV;
                a.type          = "VEC3";
                a.hasMinMax     = true;
                for (int k = 0; k < 3; ++k) { a.mn[k] = mn[k]; a.mx[k] = mx[k]; }
                accessors.push_back(a);

                // stash for MeshEntry construction below
                meshEntries.push_back({
                    region.id,
                    { region.color.x, region.color.y, region.color.z },
                    accPosIdx, 0u, 0u   // nrm/idx filled in next
                });
            }

            // Normals (VEC3 FLOAT, ARRAY_BUFFER).
            {
                const auto byteOff = static_cast<uint32_t>(binBuf.size());
                for (const auto& v : mesh.vertices) {
                    appendFloat(binBuf, v.normal.x);
                    appendFloat(binBuf, v.normal.y);
                    appendFloat(binBuf, v.normal.z);
                }
                const auto byteLen = numV * 3u * 4u;

                const auto bvIdx = static_cast<uint32_t>(bufViews.size());
                bufViews.push_back({ byteOff, byteLen, 34962u });

                const auto accNrmIdx = static_cast<uint32_t>(accessors.size());
                accessors.push_back({ bvIdx, 5126u, numV, "VEC3" });

                meshEntries.back().accNrm = accNrmIdx;
            }

            // Indices (SCALAR UNSIGNED_INT, ELEMENT_ARRAY_BUFFER).
            {
                const auto byteOff = static_cast<uint32_t>(binBuf.size());
                for (const auto idx : mesh.indices)
                    appendUint32(binBuf, idx);
                const auto byteLen = numI * 4u;

                const auto bvIdx = static_cast<uint32_t>(bufViews.size());
                bufViews.push_back({ byteOff, byteLen, 34963u });

                const auto accIdxIdx = static_cast<uint32_t>(accessors.size());
                accessors.push_back({ bvIdx, 5125u, numI, "SCALAR" });

                meshEntries.back().accIdx = accIdxIdx;
            }
        }

        if (meshEntries.empty()) return false;

        // Write binary sidecar (.bin).
        {
            std::ofstream binFile(binPath, std::ios::binary | std::ios::trunc);
            if (!binFile.is_open()) return false;
            binFile.write(
                reinterpret_cast<const char*>(binBuf.data()),
                static_cast<std::streamsize>(binBuf.size()));
            if (!binFile.good()) return false;
        }

        // Build GLTF JSON.
        const std::size_t N = meshEntries.size();
        std::ostringstream j;

        j << "{\n";
        j << "  \"asset\": { \"version\": \"2.0\", \"generator\": \"pelpaint\" },\n";
        j << "  \"scene\": 0,\n";

        // scenes — flat list of node indices [0, 1, 2, …]
        j << "  \"scenes\": [{ \"nodes\": [";
        for (std::size_t i = 0; i < N; ++i) { if (i) j << ", "; j << i; }
        j << "] }],\n";

        // nodes — one per mesh, no transform
        j << "  \"nodes\": [\n";
        for (std::size_t i = 0; i < N; ++i) {
            if (i) j << ",\n";
            j << "    { \"mesh\": " << i << " }";
        }
        j << "\n  ],\n";

        // meshes
        j << "  \"meshes\": [\n";
        for (std::size_t i = 0; i < N; ++i) {
            const auto& me = meshEntries[i];
            if (i) j << ",\n";
            j << "    {\n";
            j << "      \"name\": \"region_" << me.regionId << "\",\n";
            j << "      \"primitives\": [{\n";
            j << "        \"attributes\": {\n";
            j << "          \"POSITION\": " << me.accPos << ",\n";
            j << "          \"NORMAL\": "   << me.accNrm << "\n";
            j << "        },\n";
            j << "        \"indices\": "  << me.accIdx << ",\n";
            j << "        \"material\": " << i           << ",\n";
            j << "        \"mode\": 4\n";
            j << "      }]\n";
            j << "    }";
        }
        j << "\n  ],\n";

        // materials — one flat PBR entry per region
        j << "  \"materials\": [\n";
        for (std::size_t i = 0; i < N; ++i) {
            const auto& me = meshEntries[i];
            if (i) j << ",\n";
            const float r = me.color[0] / 255.f;
            const float g = me.color[1] / 255.f;
            const float b = me.color[2] / 255.f;
            j << "    {\n";
            j << "      \"name\": \"mat_" << me.regionId << "\",\n";
            j << "      \"pbrMetallicRoughness\": {\n";
            j << "        \"baseColorFactor\": ["
              << ff(r) << ", " << ff(g) << ", " << ff(b) << ", 1.0],\n";
            j << "        \"metallicFactor\": 0.0,\n";
            j << "        \"roughnessFactor\": 1.0\n";
            j << "      }\n";
            j << "    }";
        }
        j << "\n  ],\n";

        // accessors
        j << "  \"accessors\": [\n";
        for (std::size_t i = 0; i < accessors.size(); ++i) {
            const auto& acc = accessors[i];
            if (i) j << ",\n";
            j << "    {\n";
            j << "      \"bufferView\": "    << acc.bufViewIdx    << ",\n";
            j << "      \"byteOffset\": 0,\n";
            j << "      \"componentType\": " << acc.componentType << ",\n";
            j << "      \"count\": "         << acc.count         << ",\n";
            j << "      \"type\": \""        << acc.type          << "\"";
            if (acc.hasMinMax) {
                // min and max are required for POSITION by the GLTF spec
                j << ",\n";
                j << "      \"min\": ["
                  << ff(acc.mn[0]) << ", " << ff(acc.mn[1]) << ", " << ff(acc.mn[2]) << "],\n";
                j << "      \"max\": ["
                  << ff(acc.mx[0]) << ", " << ff(acc.mx[1]) << ", " << ff(acc.mx[2]) << "]\n";
            } else {
                j << "\n";
            }
            j << "    }";
        }
        j << "\n  ],\n";

        // bufferViews
        j << "  \"bufferViews\": [\n";
        for (std::size_t i = 0; i < bufViews.size(); ++i) {
            const auto& bv = bufViews[i];
            if (i) j << ",\n";
            j << "    {\n";
            j << "      \"buffer\": 0,\n";
            j << "      \"byteOffset\": " << bv.byteOffset << ",\n";
            j << "      \"byteLength\": " << bv.byteLength << ",\n";
            j << "      \"target\": "     << bv.target     << "\n";
            j << "    }";
        }
        j << "\n  ],\n";

        // buffers — one .bin file
        j << "  \"buffers\": [{\n";
        j << "    \"uri\": \""       << binName        << "\",\n";
        j << "    \"byteLength\": "  << binBuf.size()  << "\n";
        j << "  }]\n";

        j << "}\n";

        // Write JSON file (.gltf).
        std::ofstream gltfFile(gltfPath, std::ios::trunc);
        if (!gltfFile.is_open()) return false;
        gltfFile << j.str();
        return gltfFile.good();
    }

private:
    // Little-endian binary serialisation.

    /// Append a float as 4 little-endian bytes (IEEE 754).
    static void appendFloat(std::vector<uint8_t>& buf, float v) noexcept
    {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        buf.push_back(static_cast<uint8_t>( bits        & 0xFFu));
        buf.push_back(static_cast<uint8_t>((bits >>  8) & 0xFFu));
        buf.push_back(static_cast<uint8_t>((bits >> 16) & 0xFFu));
        buf.push_back(static_cast<uint8_t>((bits >> 24) & 0xFFu));
    }

    /// Append a uint32 as 4 little-endian bytes.
    static void appendUint32(std::vector<uint8_t>& buf, uint32_t v) noexcept
    {
        buf.push_back(static_cast<uint8_t>( v        & 0xFFu));
        buf.push_back(static_cast<uint8_t>((v >>  8) & 0xFFu));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }

    // Float → compact JSON string.

    /// Format a float for JSON output.
    /// Uses 6 decimal places then strips trailing zeros, always
    /// preserving at least one digit after the decimal point.
    ///
    ///   ff(1.0f)      → "1.0"
    ///   ff(0.5f)      → "0.5"
    ///   ff(0.333333f) → "0.333333"
    static std::string ff(float v)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision(6) << v;
        std::string str = s.str();

        const auto dot = str.find('.');
        if (dot != std::string::npos) {
            auto last = str.find_last_not_of('0');
            if (last == dot) ++last;  // keep exactly one digit after the dot
            str.erase(last + 1u);
        }
        return str;
    }
};

} // namespace pelpaint::exporter
