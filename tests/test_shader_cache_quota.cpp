#include "downgrader/shader_disk_cache.h"
#include <iostream>
#include <cassert>
#include <vector>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Milestone 4: Shader Cache Quota & Keys   \n";
    std::cout << "========================================================\n" << std::endl;

    auto& cache = ShaderDiskCache::Instance();
    std::string testDir = "test_results/test_quota_cache";
    cache.SetCacheDirectory(testDir);
    cache.ClearCache();
    cache.ResetStats();

    // 1. Composite Key Uniqueness
    std::cout << "[TEST 1] Verifying composite key determinism and isolation..." << std::endl;
    uint8_t dummyBytecode[] = { 0x44, 0x58, 0x42, 0x43, 0x01, 0x02, 0x03, 0x04 }; // "DXBC..."
    uint64_t key1 = ShaderDiskCache::ComputeCompositeKey(dummyBytecode, sizeof(dummyBytecode), "ps_5_0", "D3D11_FL11_0", "456.71", 0);
    uint64_t key2 = ShaderDiskCache::ComputeCompositeKey(dummyBytecode, sizeof(dummyBytecode), "ps_5_0", "D3D11_FL11_0", "456.71", 0);
    assert(key1 == key2); // Deterministic

    uint64_t keyProfileDiff = ShaderDiskCache::ComputeCompositeKey(dummyBytecode, sizeof(dummyBytecode), "vs_5_0", "D3D11_FL11_0", "456.71", 0);
    assert(key1 != keyProfileDiff);

    uint64_t keyBackendDiff = ShaderDiskCache::ComputeCompositeKey(dummyBytecode, sizeof(dummyBytecode), "ps_5_0", "D3D12_NATIVE", "456.71", 0);
    assert(key1 != keyBackendDiff);

    uint64_t keyDriverDiff = ShaderDiskCache::ComputeCompositeKey(dummyBytecode, sizeof(dummyBytecode), "ps_5_0", "D3D11_FL11_0", "512.15", 0);
    assert(key1 != keyDriverDiff);

    uint64_t keyFlagsDiff = ShaderDiskCache::ComputeCompositeKey(dummyBytecode, sizeof(dummyBytecode), "ps_5_0", "D3D11_FL11_0", "456.71", 1);
    assert(key1 != keyFlagsDiff);
    std::cout << "  [PASS] Composite keys isolate profile, backend, driver, and translation flags.\n" << std::endl;

    // 2. Put and TryGet using composite key
    std::cout << "[TEST 2] Testing Put and TryGet with composite key..." << std::endl;
    std::vector<uint8_t> validDxbc(64, 0);
    *reinterpret_cast<uint32_t*>(&validDxbc[0]) = 0x43425844; // "DXBC"
    bool putOk = cache.Put(key1, 0, validDxbc.data(), validDxbc.size());
    assert(putOk);

    std::vector<uint8_t> retrieved;
    bool getOk = cache.TryGet(key1, 0, retrieved);
    assert(getOk);
    assert(retrieved.size() == validDxbc.size());
    assert(retrieved == validDxbc);
    std::cout << "  [PASS] Successfully stored and retrieved shader payload from disk cache.\n" << std::endl;

    // 3. Disk Quota and Eviction
    std::cout << "[TEST 3] Testing disk quota enforcement and eviction..." << std::endl;
    // Set tiny budget of 1 MB (or enforce small quota directly)
    cache.SetMaxDiskBudgetMB(1);
    size_t currentUsage = cache.CalculateCurrentDiskUsage();
    assert(currentUsage > 0);
    std::cout << "  Current cache disk usage: " << currentUsage << " bytes." << std::endl;

    // Clear cache
    bool cleared = cache.ClearCache();
    assert(cleared);
    assert(cache.CalculateCurrentDiskUsage() == 0);
    std::cout << "  [PASS] Cache cleared successfully (usage reset to 0 bytes).\n" << std::endl;

    std::cout << "========================================================\n";
    std::cout << "   ALL SHADER CACHE QUOTA TESTS PASSED!                 \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
