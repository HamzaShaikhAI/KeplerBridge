#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>

namespace kepler {

struct MeshVertex {
    float position[4];
    float color[4];
};

struct MeshShaderDesc {
    std::string topology = "triangle"; // "triangle", "line", "point"
    uint32_t maxVertices = 3;          // Up to 64
    uint32_t maxPrimitives = 1;        // Up to 126
    bool hasAmplificationShader = false;
    bool usesDynamicPayload = false;
};

class MeshShaderCompatEngine {
public:
    static MeshShaderCompatEngine& Instance();

    /**
     * @brief Evaluates whether a mesh shader pipeline falls within the verified bounded prototype subset.
     */
    bool CanLowerMeshShader(const MeshShaderDesc& desc, std::string& outDiagnostic) const;

    /**
     * @brief Generates D3D11 CS 5.0 and VS 5.0 shader source for transforming bounded meshlets
     *        into D3D11 Input Assembler geometry.
     */
    bool GenerateTransformationPipeline(
        const MeshShaderDesc& desc,
        std::string& outComputeShaderHLSL,
        std::string& outVertexShaderHLSL,
        std::string& outDiagnostic) const;

private:
    MeshShaderCompatEngine() = default;
    ~MeshShaderCompatEngine() = default;
};

} // namespace kepler
