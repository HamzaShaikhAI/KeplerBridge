#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <cstring>

namespace kepler {

#pragma pack(push, 1)
struct ShaderCacheHeader {
    char magic[4];          // "KPSC" (Kepler Shader Cache)
    uint32_t version;       // Cache format version = 1
    uint32_t compilerFlags; // D3DCompile flags
    uint32_t bytecodeSize;  // Payload byte length
    uint64_t sourceHash;    // 64-bit FNV-1a hash of (source + entry + profile + salt)
    uint64_t payloadHash;   // 64-bit FNV-1a hash of the compiled bytecode
};
#pragma pack(pop)

class ShaderDiskCache {
public:
    static constexpr uint32_t CURRENT_CACHE_VERSION = 1;
    static constexpr char CACHE_MAGIC[4] = { 'K', 'P', 'S', 'C' };

    static ShaderDiskCache& Instance();

    void SetCacheDirectory(const std::string& dirPath);
    const std::string& GetCacheDirectory() const;

    // Compute deterministic 64-bit FNV-1a hash
    static uint64_t ComputeHash64(const void* data, size_t length, uint64_t seed = 14695981039346656037ULL);

    // Compute key for a shader compilation request
    static uint64_t ComputeShaderKey(
        const std::string& hlslSource,
        const std::string& entryPoint,
        const std::string& targetProfile,
        uint32_t compilerFlags,
        uint32_t compilerVersionSalt = 47);

    // Look up shader bytecode from cache
    bool TryGet(
        uint64_t shaderKey,
        uint32_t expectedCompilerFlags,
        std::vector<uint8_t>& outBytecode);

    // Store shader bytecode to cache atomically
    bool Put(
        uint64_t shaderKey,
        uint32_t compilerFlags,
        const void* bytecodeData,
        size_t bytecodeSize);

    // Stats & Telemetry
    uint64_t GetHits() const { return m_hits.load(); }
    uint64_t GetMisses() const { return m_misses.load(); }
    uint64_t GetWrites() const { return m_writes.load(); }
    uint64_t GetTotalLookupTimeUs() const { return m_totalLookupTimeUs.load(); }
    uint64_t GetLastLookupTimeUs() const { return m_lastLookupTimeUs.load(); }
    uint64_t GetTotalWriteTimeUs() const { return m_totalWriteTimeUs.load(); }
    uint64_t GetLastWriteTimeUs() const { return m_lastWriteTimeUs.load(); }

    void ResetStats() {
        m_hits.store(0);
        m_misses.store(0);
        m_writes.store(0);
        m_totalLookupTimeUs.store(0);
        m_lastLookupTimeUs.store(0);
        m_totalWriteTimeUs.store(0);
        m_lastWriteTimeUs.store(0);
    }

private:
    ShaderDiskCache();
    ~ShaderDiskCache() = default;
    ShaderDiskCache(const ShaderDiskCache&) = delete;
    ShaderDiskCache& operator=(const ShaderDiskCache&) = delete;

    std::string GetFilePathForKey(uint64_t key) const;
    bool EnsureDirectoryExists(const std::string& path) const;

    std::string m_cacheDir;
    mutable std::mutex m_mutex;
    std::atomic<uint64_t> m_hits{ 0 };
    std::atomic<uint64_t> m_misses{ 0 };
    std::atomic<uint64_t> m_writes{ 0 };
    std::atomic<uint64_t> m_totalLookupTimeUs{ 0 };
    std::atomic<uint64_t> m_lastLookupTimeUs{ 0 };
    std::atomic<uint64_t> m_totalWriteTimeUs{ 0 };
    std::atomic<uint64_t> m_lastWriteTimeUs{ 0 };
    std::atomic<uint32_t> m_tempCounter{ 0 };
};

} // namespace kepler
