#include "downgrader/shader_disk_cache.h"
#include "core/logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace kepler {

ShaderDiskCache& ShaderDiskCache::Instance() {
    static ShaderDiskCache s_instance;
    return s_instance;
}

ShaderDiskCache::ShaderDiskCache() {
    // Default cache directory is kepler_cache/shaders relative to working/executable directory
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeStr(exePath);
    size_t lastSlash = exeStr.find_last_of("\\/");
    std::string baseDir = (lastSlash != std::string::npos ? exeStr.substr(0, lastSlash) : ".");
    m_cacheDir = baseDir + "\\kepler_cache\\shaders";
}

void ShaderDiskCache::SetCacheDirectory(const std::string& dirPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cacheDir = dirPath;
}

const std::string& ShaderDiskCache::GetCacheDirectory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cacheDir;
}

void ShaderDiskCache::SetMaxDiskBudgetMB(size_t budgetMB) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxDiskBudgetMB = budgetMB;
}

size_t ShaderDiskCache::GetMaxDiskBudgetMB() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxDiskBudgetMB;
}

uint64_t ShaderDiskCache::ComputeHash64(const void* data, size_t length, uint64_t seed) {
    if (!data || length == 0) return 0;
    uint64_t hash = seed;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint64_t>(p[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t ShaderDiskCache::ComputeShaderKey(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& targetProfile,
    uint32_t compilerFlags,
    uint32_t compilerVersionSalt)
{
    uint64_t hash = 14695981039346656037ULL;
    hash = ComputeHash64(hlslSource.data(), hlslSource.size(), hash);
    const char sep = '\0';
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(entryPoint.data(), entryPoint.size(), hash);
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(targetProfile.data(), targetProfile.size(), hash);
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(&compilerFlags, sizeof(compilerFlags), hash);
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(&compilerVersionSalt, sizeof(compilerVersionSalt), hash);
    return hash;
}

uint64_t ShaderDiskCache::ComputeCompositeKey(
    const void* inputBytecode,
    size_t inputSize,
    const std::string& targetProfile,
    const std::string& backendId,
    const std::string& driverId,
    uint32_t translationFlags,
    uint32_t engineVersion)
{
    uint64_t hash = 14695981039346656037ULL;
    if (inputBytecode && inputSize > 0) {
        hash = ComputeHash64(inputBytecode, inputSize, hash);
    }
    const char sep = '\0';
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(targetProfile.data(), targetProfile.size(), hash);
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(backendId.data(), backendId.size(), hash);
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(driverId.data(), driverId.size(), hash);
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(&translationFlags, sizeof(translationFlags), hash);
    hash = ComputeHash64(&sep, 1, hash);
    hash = ComputeHash64(&engineVersion, sizeof(engineVersion), hash);
    return hash;
}

std::string ShaderDiskCache::GetFilePathForKey(uint64_t key) const {
    std::ostringstream ss;
    ss << m_cacheDir << "\\" << std::hex << std::setw(16) << std::setfill('0') << key << ".dxbc";
    return ss.str();
}

bool ShaderDiskCache::EnsureDirectoryExists(const std::string& path) const {
    if (CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }

    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        std::string parent = path.substr(0, lastSlash);
        if (!EnsureDirectoryExists(parent)) {
            return false;
        }
    }
    return CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ShaderDiskCache::TryGet(
    uint64_t shaderKey,
    uint32_t expectedCompilerFlags,
    std::vector<uint8_t>& outBytecode)
{
    auto tStart = std::chrono::high_resolution_clock::now();
    auto recordElapsed = [this, &tStart]() {
        uint64_t elapsedUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - tStart).count());
        m_lastLookupTimeUs.store(elapsedUs);
        m_totalLookupTimeUs.fetch_add(elapsedUs);
    };

    outBytecode.clear();
    std::string filePath;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        filePath = GetFilePathForKey(shaderKey);
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        m_misses.fetch_add(1);
        recordElapsed();
        return false;
    }

    ShaderCacheHeader header = {};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good()) {
        file.close();
        m_misses.fetch_add(1);
        m_invalidations.fetch_add(1);
        recordElapsed();
        return false;
    }

    // Validate magic and version
    if (std::memcmp(header.magic, CACHE_MAGIC, sizeof(CACHE_MAGIC)) != 0 ||
        header.version != CURRENT_CACHE_VERSION)
    {
        file.close();
        m_misses.fetch_add(1);
        m_invalidations.fetch_add(1);
        recordElapsed();
        return false;
    }

    // Validate compiler flags and source hash
    if (header.compilerFlags != expectedCompilerFlags ||
        header.sourceHash != shaderKey)
    {
        file.close();
        m_misses.fetch_add(1);
        m_invalidations.fetch_add(1);
        recordElapsed();
        return false;
    }

    // Sanity check bytecode size (< 16 MB)
    if (header.bytecodeSize == 0 || header.bytecodeSize > 16 * 1024 * 1024) {
        file.close();
        m_misses.fetch_add(1);
        m_invalidations.fetch_add(1);
        recordElapsed();
        return false;
    }

    outBytecode.resize(header.bytecodeSize);
    file.read(reinterpret_cast<char*>(outBytecode.data()), header.bytecodeSize);
    if (!file.good()) {
        outBytecode.clear();
        file.close();
        m_misses.fetch_add(1);
        m_invalidations.fetch_add(1);
        recordElapsed();
        return false;
    }
    file.close();

    // Validate payload cryptographic checksum
    uint64_t actualPayloadHash = ComputeHash64(outBytecode.data(), outBytecode.size());
    if (actualPayloadHash != header.payloadHash) {
        LOG_WARN("[ShaderDiskCache] Corrupted cache entry detected for key 0x" +
                 Logger::HexToString(shaderKey) + ". Removing.");
        outBytecode.clear();
        DeleteFileA(filePath.c_str());
        m_misses.fetch_add(1);
        m_invalidations.fetch_add(1);
        recordElapsed();
        return false;
    }

    // Validate DXBC container magic ('DXBC' = 0x43425844)
    if (outBytecode.size() < 4) {
        outBytecode.clear();
        m_misses.fetch_add(1);
        m_invalidations.fetch_add(1);
        recordElapsed();
        return false;
    }
    uint32_t magic = *reinterpret_cast<const uint32_t*>(outBytecode.data());
    if (magic != 0x43425844) {
        LOG_WARN("[ShaderDiskCache] Cache payload does not contain DXBC magic header.");
        outBytecode.clear();
        DeleteFileA(filePath.c_str());
        m_misses.fetch_add(1);
        m_invalidations.fetch_add(1);
        recordElapsed();
        return false;
    }

    m_hits.fetch_add(1);
    recordElapsed();
    return true;
}

bool ShaderDiskCache::Put(
    uint64_t shaderKey,
    uint32_t compilerFlags,
    const void* bytecodeData,
    size_t bytecodeSize)
{
    auto tStart = std::chrono::high_resolution_clock::now();
    if (!bytecodeData || bytecodeSize == 0 || bytecodeSize > 16 * 1024 * 1024) {
        return false;
    }

    // Must be valid DXBC
    if (bytecodeSize < 4) return false;
    uint32_t magic = *reinterpret_cast<const uint32_t*>(bytecodeData);
    if (magic != 0x43425844) {
        LOG_WARN("[ShaderDiskCache] Attempted to cache non-DXBC bytecode.");
        return false;
    }

    std::string dirPath;
    std::string targetFilePath;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dirPath = m_cacheDir;
        targetFilePath = GetFilePathForKey(shaderKey);
    }

    if (!EnsureDirectoryExists(dirPath)) {
        LOG_WARN("[ShaderDiskCache] Failed to ensure cache directory: " + dirPath);
        return false;
    }

    // Check disk quota before writing new file
    EnforceDiskQuota();

    // Atomic write pattern: Write to temporary file, then MoveFileEx with REPLACE_EXISTING
    uint32_t tempId = m_tempCounter.fetch_add(1);
    std::ostringstream ssTmp;
    ssTmp << targetFilePath << ".tmp_" << GetCurrentProcessId() << "_" << tempId;
    std::string tempFilePath = ssTmp.str();

    std::ofstream file(tempFilePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        LOG_WARN("[ShaderDiskCache] Failed to create temp cache file: " + tempFilePath);
        return false;
    }

    ShaderCacheHeader header = {};
    std::memcpy(header.magic, CACHE_MAGIC, sizeof(CACHE_MAGIC));
    header.version = CURRENT_CACHE_VERSION;
    header.compilerFlags = compilerFlags;
    header.bytecodeSize = static_cast<uint32_t>(bytecodeSize);
    header.sourceHash = shaderKey;
    header.payloadHash = ComputeHash64(bytecodeData, bytecodeSize);

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(bytecodeData), bytecodeSize);
    file.flush();
    file.close();

    // Atomic rename
    if (!MoveFileExA(tempFilePath.c_str(), targetFilePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileA(tempFilePath.c_str());
        LOG_WARN("[ShaderDiskCache] Failed to atomically rename temp file to: " + targetFilePath);
        return false;
    }

    uint64_t elapsedUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - tStart).count());
    m_lastWriteTimeUs.store(elapsedUs);
    m_totalWriteTimeUs.fetch_add(elapsedUs);
    m_writes.fetch_add(1);
    return true;
}

size_t ShaderDiskCache::CalculateCurrentDiskUsage() const {
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dir = m_cacheDir;
    }

    WIN32_FIND_DATAA findData;
    std::string searchPattern = dir + "\\*.dxbc";
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    size_t totalBytes = 0;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = findData.nFileSizeLow;
            fileSize.HighPart = findData.nFileSizeHigh;
            totalBytes += static_cast<size_t>(fileSize.QuadPart);
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return totalBytes;
}

size_t ShaderDiskCache::EnforceDiskQuota() {
    std::string dir;
    size_t budgetMB;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dir = m_cacheDir;
        budgetMB = m_maxDiskBudgetMB;
    }

    size_t budgetBytes = budgetMB * 1024 * 1024;
    struct FileEntry {
        std::string path;
        FILETIME lastWrite;
        size_t sizeBytes;
    };

    std::vector<FileEntry> files;
    size_t totalBytes = 0;

    WIN32_FIND_DATAA findData;
    std::string searchPattern = dir + "\\*.dxbc";
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = findData.nFileSizeLow;
            fileSize.HighPart = findData.nFileSizeHigh;
            size_t sz = static_cast<size_t>(fileSize.QuadPart);
            totalBytes += sz;
            files.push_back({ dir + "\\" + findData.cFileName, findData.ftLastWriteTime, sz });
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);

    if (totalBytes <= budgetBytes) return 0;

    // Sort oldest files first
    std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
        return CompareFileTime(&a.lastWrite, &b.lastWrite) < 0;
    });

    size_t evictedCount = 0;
    for (const auto& f : files) {
        if (totalBytes <= budgetBytes) break;
        if (DeleteFileA(f.path.c_str())) {
            totalBytes -= f.sizeBytes;
            evictedCount++;
            m_evictions.fetch_add(1);
        }
    }

    return evictedCount;
}

bool ShaderDiskCache::ClearCache() {
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dir = m_cacheDir;
    }

    WIN32_FIND_DATAA findData;
    std::string searchPattern = dir + "\\*.*";
    HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string filePath = dir + "\\" + findData.cFileName;
            DeleteFileA(filePath.c_str());
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return true;
}

} // namespace kepler
