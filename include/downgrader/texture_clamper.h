#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <string>
#include <cstdint>

namespace kepler {

struct ResourceValidationResult {
    bool isValid = true;
    bool isModified = false;
    D3D12_RESOURCE_DESC modifiedDesc{};
    std::string diagnostics;
};

class TextureClamper {
public:
    static TextureClamper& Instance();

    // Validates a resource descriptor against Kepler hardware limits and active configuration
    ResourceValidationResult ValidateAndClamp(
        const D3D12_RESOURCE_DESC* pDesc,
        const D3D12_HEAP_PROPERTIES* pHeapProps,
        D3D12_HEAP_FLAGS heapFlags);

    // Formats a human-readable summary of resource dimensions, format, and flags
    static std::string ResourceDescToString(const D3D12_RESOURCE_DESC* pDesc);

private:
    TextureClamper() = default;
    ~TextureClamper() = default;
    TextureClamper(const TextureClamper&) = delete;
    TextureClamper& operator=(const TextureClamper&) = delete;
};

} // namespace kepler
