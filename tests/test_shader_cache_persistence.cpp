#include "downgrader/bounded_shader_translator.h"
#include "downgrader/shader_disk_cache.h"
#include "core/config.h"

#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <filesystem>

using namespace kepler;

static const char* s_testHLSL = R"(
    cbuffer Params : register(b0) {
        float4 g_tint;
    };
    struct VSInput {
        float3 position : POSITION;
        float2 uv : TEXCOORD;
    };
    struct PSInput {
        float4 position : SV_POSITION;
        float2 uv : TEXCOORD;
    };
    PSInput VSMain(VSInput input) {
        PSInput output;
        output.position = float4(input.position, 1.0f);
        output.uv = input.uv;
        return output;
    }
    float4 PSMain(PSInput input) : SV_TARGET {
        return float4(input.uv, 0.5f, 1.0f) * g_tint;
    }
)";

static std::string GetExeDirectory() {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeStr(exePath);
    size_t lastSlash = exeStr.find_last_of("\\/");
    return (lastSlash != std::string::npos ? exeStr.substr(0, lastSlash) : ".");
}

static bool RunSubprocess(const std::string& arg) {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    std::string cmd = std::string("\"") + exePath + "\" " + arg;
    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessA(
        NULL,
        const_cast<char*>(cmd.c_str()),
        NULL, NULL, FALSE, 0, NULL, NULL,
        &si, &pi);

    if (!ok) {
        std::cerr << "[PARENT] Failed to spawn subprocess: " << cmd << " (Error: " << GetLastError() << ")" << std::endl;
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return (exitCode == 0);
}

int RunColdChild(const std::string& cacheDir, const std::string& outBinPath) {
    auto& cache = ShaderDiskCache::Instance();
    cache.SetCacheDirectory(cacheDir);
    cache.ResetStats();

    BoundedShaderTranslator translator;
    translator.ResetStats();

    std::vector<uint8_t> bytecode;
    std::string errMsg;

    auto tStart = std::chrono::high_resolution_clock::now();
    bool ok = translator.CompileHLSLToDXBC(s_testHLSL, "PSMain", "ps_5_0", bytecode, errMsg);
    auto tEnd = std::chrono::high_resolution_clock::now();

    if (!ok || bytecode.empty()) {
        std::cerr << "[CHILD-COLD] Compilation failed: " << errMsg << std::endl;
        return 1;
    }

    uint64_t durUs = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
    std::cout << "[CHILD-COLD] Cold compilation completed in " << (durUs / 1000.0) << " ms ("
              << bytecode.size() << " bytes)." << std::endl;

    // Must be compiled (not bypassed)
    if (translator.GetCompileCount() != 1 || translator.GetCacheBypassCount() != 0) {
        std::cerr << "[CHILD-COLD] Expected 1 compile and 0 bypass, got "
                  << translator.GetCompileCount() << " and " << translator.GetCacheBypassCount() << std::endl;
        return 1;
    }

    // Save cold bytecode to file for parent verification
    std::ofstream out(outBinPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytecode.data()), bytecode.size());
    out.close();

    return 0;
}

int RunWarmChild(const std::string& cacheDir, const std::string& outBinPath, const std::string& compareBinPath) {
    auto& cache = ShaderDiskCache::Instance();
    cache.SetCacheDirectory(cacheDir);
    cache.ResetStats();

    BoundedShaderTranslator translator;
    translator.ResetStats();

    std::vector<uint8_t> bytecode;
    std::string errMsg;

    auto tStart = std::chrono::high_resolution_clock::now();
    bool ok = translator.CompileHLSLToDXBC(s_testHLSL, "PSMain", "ps_5_0", bytecode, errMsg);
    auto tEnd = std::chrono::high_resolution_clock::now();

    if (!ok || bytecode.empty()) {
        std::cerr << "[CHILD-WARM] Warm lookup failed: " << errMsg << std::endl;
        return 1;
    }

    uint64_t durUs = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
    std::cout << "[CHILD-WARM] Warm cache hit completed in " << (durUs / 1000.0) << " ms ("
              << durUs << " us) - Compiler Bypassed!" << std::endl;

    // Must be bypassed (0 compile, 1 bypass, 1 cache hit)
    if (translator.GetCompileCount() != 0) {
        std::cerr << "[CHILD-WARM] Expected 0 compilations, got " << translator.GetCompileCount() << std::endl;
        return 1;
    }
    if (translator.GetCacheBypassCount() != 1) {
        std::cerr << "[CHILD-WARM] Expected 1 cache bypass, got " << translator.GetCacheBypassCount() << std::endl;
        return 1;
    }
    if (cache.GetHits() != 1) {
        std::cerr << "[CHILD-WARM] Expected 1 cache hit, got " << cache.GetHits() << std::endl;
        return 1;
    }

    // Compare with cold bytecode
    std::ifstream inCold(compareBinPath, std::ios::binary);
    if (!inCold.is_open()) {
        std::cerr << "[CHILD-WARM] Failed to open cold bytecode reference file." << std::endl;
        return 1;
    }
    std::vector<uint8_t> coldBytecode((std::istreambuf_iterator<char>(inCold)), std::istreambuf_iterator<char>());
    inCold.close();

    if (coldBytecode.size() != bytecode.size()) {
        std::cerr << "[CHILD-WARM] Bytecode size mismatch: cold=" << coldBytecode.size()
                  << ", warm=" << bytecode.size() << std::endl;
        return 1;
    }
    if (std::memcmp(coldBytecode.data(), bytecode.data(), bytecode.size()) != 0) {
        std::cerr << "[CHILD-WARM] Bytecode content mismatch between cold and warm runs!" << std::endl;
        return 1;
    }

    std::cout << "[CHILD-WARM] Bytecode verified byte-for-byte identical with cold compilation reference!" << std::endl;

    std::ofstream out(outBinPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytecode.data()), bytecode.size());
    out.close();

    return 0;
}

int RunFlagMismatchChild(const std::string& cacheDir) {
    auto& cache = ShaderDiskCache::Instance();
    cache.SetCacheDirectory(cacheDir);
    cache.ResetStats();

    // Query cache directly with modified flags
    uint32_t standardFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    uint64_t standardKey = ShaderDiskCache::ComputeShaderKey(s_testHLSL, "PSMain", "ps_5_0", standardFlags);

    uint32_t modifiedFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION;
    uint64_t modifiedKey = ShaderDiskCache::ComputeShaderKey(s_testHLSL, "PSMain", "ps_5_0", modifiedFlags);

    std::vector<uint8_t> bytecode;
    // Looking up with modifiedKey must return false (cache miss)
    bool found = cache.TryGet(modifiedKey, modifiedFlags, bytecode);
    if (found) {
        std::cerr << "[CHILD-FLAG] Expected cache miss for modified flags, but found an entry!" << std::endl;
        return 1;
    }
    if (cache.GetMisses() != 1) {
        std::cerr << "[CHILD-FLAG] Expected 1 miss counter increment, got " << cache.GetMisses() << std::endl;
        return 1;
    }

    std::cout << "[CHILD-FLAG] Flag mismatch correctly produced cache miss and forced re-evaluation." << std::endl;
    return 0;
}

int RunCorruptRecoveryChild(const std::string& cacheDir) {
    auto& cache = ShaderDiskCache::Instance();
    cache.SetCacheDirectory(cacheDir);
    cache.ResetStats();

    uint32_t standardFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    uint64_t key = ShaderDiskCache::ComputeShaderKey(s_testHLSL, "PSMain", "ps_5_0", standardFlags);

    std::vector<uint8_t> bytecode;
    // Corrupted entry should be detected, evicted/deleted, and return false
    bool found = cache.TryGet(key, standardFlags, bytecode);
    if (found) {
        std::cerr << "[CHILD-CORRUPT] Corrupted entry was accepted without rejection!" << std::endl;
        return 1;
    }

    // Now recompile via BoundedShaderTranslator and verify it recovers and writes fresh valid entry
    BoundedShaderTranslator translator;
    std::string errMsg;
    bool ok = translator.CompileHLSLToDXBC(s_testHLSL, "PSMain", "ps_5_0", bytecode, errMsg);
    if (!ok || bytecode.empty()) {
        std::cerr << "[CHILD-CORRUPT] Recompilation recovery failed: " << errMsg << std::endl;
        return 1;
    }

    // Subsequent TryGet must succeed
    std::vector<uint8_t> freshBytecode;
    bool freshFound = cache.TryGet(key, standardFlags, freshBytecode);
    if (!freshFound || freshBytecode.size() != bytecode.size()) {
        std::cerr << "[CHILD-CORRUPT] Fresh cache entry verification failed!" << std::endl;
        return 1;
    }

    std::cout << "[CHILD-CORRUPT] Corrupt entry safely evicted, recompiled, and restored." << std::endl;
    return 0;
}

int main(int argc, char* argv[]) {
    std::string exeDir = GetExeDirectory();
    std::string testCacheDir = exeDir + "\\kepler_cache\\test_shaders";
    std::string coldBin = exeDir + "\\kepler_cache\\cold_ref.bin";
    std::string warmBin = exeDir + "\\kepler_cache\\warm_ref.bin";

    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "--cold") {
            return RunColdChild(testCacheDir, coldBin);
        } else if (mode == "--warm") {
            return RunWarmChild(testCacheDir, warmBin, coldBin);
        } else if (mode == "--flag-mismatch") {
            return RunFlagMismatchChild(testCacheDir);
        } else if (mode == "--corrupt-recovery") {
            return RunCorruptRecoveryChild(testCacheDir);
        }
    }

    // Parent Test Coordinator Mode
    std::cout << "==========================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 7 Shader Cache Multi-Process Tests   " << std::endl;
    std::cout << "==========================================================" << std::endl;

    // Clean test directory
    std::filesystem::remove_all(testCacheDir);
    std::filesystem::create_directories(testCacheDir);

    // STEP 1: Launch Cold Process
    std::cout << "\n--- STEP 1: Cold Process Compilation & Cache Write ---" << std::endl;
    bool okCold = RunSubprocess("--cold");
    if (!okCold) {
        std::cerr << "[FAIL] Cold child process exited with error!" << std::endl;
        return 1;
    }
    std::cout << "[PASS] Cold process compiled shader, wrote cache entry, and exited cleanly." << std::endl;

    // STEP 2: Verify Cache File on Disk
    std::cout << "\n--- STEP 2: Cache Entry On-Disk Verification ---" << std::endl;
    uint32_t compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    uint64_t shaderKey = ShaderDiskCache::ComputeShaderKey(s_testHLSL, "PSMain", "ps_5_0", compileFlags);

    char hexBuf[32];
    sprintf_s(hexBuf, "%016llx.dxbc", static_cast<unsigned long long>(shaderKey));
    std::string cacheFilePath = testCacheDir + "\\" + hexBuf;

    DWORD fileAttr = GetFileAttributesA(cacheFilePath.c_str());
    if (fileAttr == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "[FAIL] Expected cache file does not exist on disk: " << cacheFilePath << std::endl;
        return 1;
    }
    std::cout << "[PASS] Verified cache entry exists on disk: " << hexBuf << std::endl;

    // STEP 3: Launch Separate Warm Process
    std::cout << "\n--- STEP 3: Warm Process Cache Hit & Compilation Bypass ---" << std::endl;
    bool okWarm = RunSubprocess("--warm");
    if (!okWarm) {
        std::cerr << "[FAIL] Warm child process failed to hit cache or bypass compiler!" << std::endl;
        return 1;
    }
    std::cout << "[PASS] Warm process verified cache hit and bypassed compiler without calling D3DCompile." << std::endl;

    // STEP 4: Launch Compiler Flag Invalidation Test
    std::cout << "\n--- STEP 4: Compiler Flag Invalidation & Miss Handling ---" << std::endl;
    bool okFlag = RunSubprocess("--flag-mismatch");
    if (!okFlag) {
        std::cerr << "[FAIL] Flag mismatch test failed!" << std::endl;
        return 1;
    }
    std::cout << "[PASS] Compiler flag mismatch properly invalidated cache lookup." << std::endl;

    // STEP 5: Corrupt Cache Entry and Test Safe Eviction & Recovery
    std::cout << "\n--- STEP 5: Corruption Detection, Eviction & Recompilation ---" << std::endl;
    {
        // Modify a payload byte to trigger checksum mismatch
        std::fstream f(cacheFilePath, std::ios::in | std::ios::out | std::ios::binary);
        if (f.is_open()) {
            f.seekp(sizeof(ShaderCacheHeader) + 10);
            char badByte = 0x55;
            f.write(&badByte, 1);
            f.close();
            std::cout << "[INFO] Injected 1-byte corruption into cache payload on disk." << std::endl;
        }
    }

    bool okCorrupt = RunSubprocess("--corrupt-recovery");
    if (!okCorrupt) {
        std::cerr << "[FAIL] Corrupt recovery test failed!" << std::endl;
        return 1;
    }
    std::cout << "[PASS] Corrupt entry was detected, safely evicted, and recompiled without failure." << std::endl;

    // Cleanup reference files
    DeleteFileA(coldBin.c_str());
    DeleteFileA(warmBin.c_str());

    std::cout << "\n==========================================================" << std::endl;
    std::cout << "  ALL MULTI-PROCESS SHADER CACHE TESTS PASSED (5/5)!      " << std::endl;
    std::cout << "==========================================================" << std::endl;
    return 0;
}
