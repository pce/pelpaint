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
#include <limits>             // std::numeric_limits
#include <string>             // std::string
#include <vector>             // std::vector

#include <nlohmann/json.hpp>

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
 * a .gltf (nlohmann/json) and a .bin (binary buffer sidecar).
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

        // Build GLTF JSON using nlohmann/json.
        using nlohmann::json;
        const std::size_t N = meshEntries.size();

        // scenes — flat list of node indices [0, 1, 2, …]
        json nodeIndices = json::array();
        for (std::size_t i = 0; i < N; ++i)
            nodeIndices.push_back(static_cast<int>(i));

        // nodes — one per mesh, no transform
        json nodes = json::array();
        for (std::size_t i = 0; i < N; ++i)
            nodes.push_back(json{{"mesh", static_cast<int>(i)}});

        // meshes — one extruded slab per region
        json meshes = json::array();
        for (std::size_t i = 0; i < N; ++i) {
            const auto& me = meshEntries[i];
            meshes.push_back({
                {"name", "region_" + std::to_string(me.regionId)},
                {"primitives", json::array({
                    {
                        {"attributes", {
                            {"POSITION", static_cast<int>(me.accPos)},
                            {"NORMAL",   static_cast<int>(me.accNrm)}
                        }},
                        {"indices",  static_cast<int>(me.accIdx)},
                        {"material", static_cast<int>(i)},
                        {"mode",     4}
                    }
                })}
            });
        }

        // materials — one flat PBR entry per region
        json materials = json::array();
        for (std::size_t i = 0; i < N; ++i) {
            const auto& me = meshEntries[i];
            const float r = me.color[0] / 255.f;
            const float g = me.color[1] / 255.f;
            const float b = me.color[2] / 255.f;
            materials.push_back({
                {"name", "mat_" + std::to_string(me.regionId)},
                {"pbrMetallicRoughness", {
                    {"baseColorFactor", {r, g, b, 1.0f}},
                    {"metallicFactor",  0.0f},
                    {"roughnessFactor", 1.0f}
                }}
            });
        }

        // accessors
        json accessorArray = json::array();
        for (const auto& acc : accessors) {
            json a = {
                {"bufferView",    static_cast<int>(acc.bufViewIdx)},
                {"byteOffset",    0},
                {"componentType", static_cast<int>(acc.componentType)},
                {"count",         static_cast<int>(acc.count)},
                {"type",          acc.type}
            };
            if (acc.hasMinMax) {
                // min and max are required for POSITION by the GLTF spec
                a["min"] = {acc.mn[0], acc.mn[1], acc.mn[2]};
                a["max"] = {acc.mx[0], acc.mx[1], acc.mx[2]};
            }
            accessorArray.push_back(a);
        }

        // bufferViews
        json bufViewArray = json::array();
        for (const auto& bv : bufViews) {
            bufViewArray.push_back({
                {"buffer",     0},
                {"byteOffset", static_cast<int>(bv.byteOffset)},
                {"byteLength", static_cast<int>(bv.byteLength)},
                {"target",     static_cast<int>(bv.target)}
            });
        }

        // Assemble the complete glTF document
        const json gltf = {
            {"asset",       {{"version", "2.0"}, {"generator", "pelpaint"}}},
            {"scene",       0},
            {"scenes",      json::array({ {{"nodes", nodeIndices}} })},
            {"nodes",       nodes},
            {"meshes",      meshes},
            {"materials",   materials},
            {"accessors",   accessorArray},
            {"bufferViews", bufViewArray},
            {"buffers",     json::array({
                {{"uri", binName}, {"byteLength", static_cast<int>(binBuf.size())}}
            })}
        };

        // Write JSON file (.gltf).
        std::ofstream gltfFile(gltfPath, std::ios::trunc);
        if (!gltfFile.is_open()) return false;
        gltfFile << gltf.dump(2);
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


};

} // namespace pelpaint::exporter
