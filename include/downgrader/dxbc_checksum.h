#pragma once

#include <cstdint>
#include <cstddef>

namespace kepler {

/**
 * @brief Computes the authentic 128-bit MD5 container checksum for Microsoft Direct3D
 *        Bytecode (DXBC) containers.
 *
 * Direct3D runtimes (D3D10, D3D11, D3D12) verify this checksum upon shader creation.
 * Bytecode with invalid checksums is rejected with E_INVALIDARG (0x80070057).
 *
 * @param pData   Pointer to raw DXBC binary buffer.
 * @param size    Total container size in bytes (must be >= 20).
 * @param outHash Output buffer for 4 DWORDs (16 bytes).
 * @return true if computation succeeded, false if invalid arguments.
 */
bool CalculateDXBCChecksum(const void* pData, size_t size, uint32_t outHash[4]);

} // namespace kepler
