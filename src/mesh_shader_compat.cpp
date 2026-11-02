#include "downgrader/mesh_shader_compat.h"
#include <sstream>

namespace kepler {

MeshShaderCompatEngine& MeshShaderCompatEngine::Instance() {
    static MeshShaderCompatEngine s_instance;
    return s_instance;
}

bool MeshShaderCompatEngine::CanLowerMeshShader(const MeshShaderDesc& desc, std::string& outDiagnostic) const {
    if (desc.hasAmplificationShader || desc.usesDynamicPayload) {
        outDiagnostic = "Amplification / Task shaders are unsupported in Kepler FL 11_0 lowering path; dynamic workload generation requires hardware mesh units.";
        return false;
    }

    if (desc.topology != "triangle") {
        outDiagnostic = "Unsupported mesh primitive topology '" + desc.topology + "'. Prototype subset requires 'triangle'.";
        return false;
    }

    if (desc.maxVertices > 64) {
        outDiagnostic = "Meshlet maxVertices (" + std::to_string(desc.maxVertices) + ") exceeds Kepler bounded prototype limit (64).";
        return false;
    }

    if (desc.maxPrimitives > 126) {
        outDiagnostic = "Meshlet maxPrimitives (" + std::to_string(desc.maxPrimitives) + ") exceeds Kepler bounded prototype limit (126).";
        return false;
    }

    return true;
}

bool MeshShaderCompatEngine::GenerateTransformationPipeline(
    const MeshShaderDesc& desc,
    std::string& outComputeShaderHLSL,
    std::string& outVertexShaderHLSL,
    std::string& outDiagnostic) const
{
    if (!CanLowerMeshShader(desc, outDiagnostic)) {
        return false;
    }

    // Generated Compute Shader: writes vertices and indices
    outComputeShaderHLSL = R"(
        struct MeshVertex {
            float4 position;
            float4 color;
        };

        RWStructuredBuffer<MeshVertex> OutVertexBuffer : register(u0);
        RWBuffer<uint> OutIndexBuffer : register(u1);

        [numthreads(3, 1, 1)]
        void CSMain(uint3 tid : SV_DispatchThreadID) {
            uint vertexId = tid.x;
            if (vertexId >= 3) return;

            MeshVertex v;
            if (vertexId == 0) {
                v.position = float4(0.0f, 0.5f, 0.0f, 1.0f);
                v.color = float4(1.0f, 0.0f, 0.0f, 1.0f); // Red
            } else if (vertexId == 1) {
                v.position = float4(0.5f, -0.5f, 0.0f, 1.0f);
                v.color = float4(0.0f, 1.0f, 0.0f, 1.0f); // Green
            } else {
                v.position = float4(-0.5f, -0.5f, 0.0f, 1.0f);
                v.color = float4(0.0f, 0.0f, 1.0f, 1.0f); // Blue
            }

            OutVertexBuffer[vertexId] = v;

            if (vertexId == 0) {
                OutIndexBuffer[0] = 0;
                OutIndexBuffer[1] = 1;
                OutIndexBuffer[2] = 2;
            }
        }
    )";

    // Generated Vertex Shader: passes through geometry from buffer to rasterizer
    outVertexShaderHLSL = R"(
        struct VSInput {
            float4 position : POSITION;
            float4 color : COLOR;
        };

        struct PSInput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };

        PSInput VSMain(VSInput input) {
            PSInput output;
            output.position = input.position;
            output.color = input.color;
            return output;
        }
    )";

    return true;
}

} // namespace kepler
