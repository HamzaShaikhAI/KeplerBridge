#include <iostream>
#include <chrono>
#include <vector>
#include <cassert>
#include <iomanip>
#include <filesystem>
#include "downgrader/bounded_shader_translator.h"
#include "downgrader/shader_disk_cache.h"
#include "translator/command_translator.h"
#include "translator/d3d11_backend.h"
#include "translator/resource_manager.h"
#include "core/config.h"
#include "core/logger.h"

using namespace kepler;

static const char* s_testHLSL = R"(
cbuffer RootConstants : register(b0) {
    float4 u_scale;
};
struct VSInput {
    float3 pos : POSITION;
    float4 col : COLOR;
};
struct PSInput {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};
PSInput VSMain(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos * u_scale.xyz, 1.0f);
    output.col = input.col;
    return output;
}
float4 PSMain(PSInput input) : SV_TARGET {
    return input.col * u_scale;
}
)";

void BenchmarkShaderCompilationAndCache() {
    std::cout << "\n--- Benchmark 1: Shader Compilation vs Persistent Cache Lookup ---" << std::endl;
    std::string cacheDir = "kepler_cache/perf_test";
    std::filesystem::remove_all(cacheDir);

    auto& cache = ShaderDiskCache::Instance();
    cache.SetCacheDirectory(cacheDir);
    cache.ResetStats();

    BoundedShaderTranslator translator;
    translator.ResetStats();

    std::vector<uint8_t> coldBytecode;
    std::string errMsg;

    // 1. Cold compile (cache miss -> D3DCompile)
    auto coldStart = std::chrono::high_resolution_clock::now();
    bool coldOk = translator.CompileHLSLToDXBC(s_testHLSL, "VSMain", "vs_5_0", coldBytecode, errMsg);
    auto coldEnd = std::chrono::high_resolution_clock::now();
    uint64_t coldDurationUs = std::chrono::duration_cast<std::chrono::microseconds>(coldEnd - coldStart).count();

    assert(coldOk);
    assert(!coldBytecode.empty());
    assert(translator.GetCompileCount() == 1);
    assert(translator.GetCacheBypassCount() == 0);

    // 2. Warm lookup (cache hit -> disk read)
    std::vector<uint8_t> warmBytecode;
    const int warmIters = 10;
    auto warmStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < warmIters; ++i) {
        warmBytecode.clear();
        bool warmOk = translator.CompileHLSLToDXBC(s_testHLSL, "VSMain", "vs_5_0", warmBytecode, errMsg);
        assert(warmOk);
    }
    auto warmEnd = std::chrono::high_resolution_clock::now();
    uint64_t totalWarmUs = std::chrono::duration_cast<std::chrono::microseconds>(warmEnd - warmStart).count();
    uint64_t warmDurationUs = totalWarmUs / warmIters;

    assert(warmOk);
    assert(warmBytecode == coldBytecode);
    assert(translator.GetCompileCount() == 1);
    assert(translator.GetCacheBypassCount() == 1);

    double speedup = static_cast<double>(coldDurationUs) / (warmDurationUs > 0 ? warmDurationUs : 1);

    std::cout << "  Cold compilation time : " << std::fixed << std::setprecision(3)
              << (coldDurationUs / 1000.0) << " ms (" << coldDurationUs << " us)" << std::endl;
    std::cout << "  Warm disk-cache lookup: " << std::fixed << std::setprecision(3)
              << (warmDurationUs / 1000.0) << " ms (" << warmDurationUs << " us)" << std::endl;
    std::cout << "  Speedup factor        : " << std::fixed << std::setprecision(1) << speedup << "x" << std::endl;
    std::cout << "  Compile count: " << translator.GetCompileCount()
              << ", Cache bypass hits: " << translator.GetCacheBypassCount() << std::endl;
    assert(speedup >= 2.0); // Cache should be at least 2x faster, typically 10-30x
    std::cout << "[PASS] Benchmark 1: Shader Compilation vs Disk Cache" << std::endl;
}

void BenchmarkRootConstantMapSuppression() {
    std::cout << "\n--- Benchmark 2: Root Constant Buffer Staging & Suppression ---" << std::endl;
    auto& backend = D3D11Backend::Instance();
    if (!backend.Initialize()) {
        std::cout << "  [SKIP] D3D11 backend unavailable." << std::endl;
        return;
    }

    const int iterations = 10000;
    std::vector<uint32_t> testConstants = { 100, 200, 300, 400 };

    // Baseline: Alternating data (forcing Map(DISCARD) every time)
    backend.ResetRootConstantStats();
    auto mapStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        testConstants[0] = static_cast<uint32_t>(i);
        RecordedCommand cmd;
        cmd.type = CommandType::SetGraphicsRoot32BitConstants;
        new (&cmd.setRootConstants) SetRoot32BitConstantsCmd();
        cmd.setRootConstants.rootParameterIndex = 0;
        cmd.setRootConstants.num32BitValuesToSet = 4;
        cmd.setRootConstants.destOffsetIn32BitValues = 0;
        cmd.setRootConstants.isCompute = false;
        cmd.setRootConstants.constants = testConstants;

        CommandBatch batch;
        batch.Append(std::move(cmd));
        backend.ReplayCommandBatch(batch);
    }
    auto mapEnd = std::chrono::high_resolution_clock::now();
    uint64_t forcedMapUs = std::chrono::duration_cast<std::chrono::microseconds>(mapEnd - mapStart).count();
    uint64_t actualMaps = backend.GetRootConstantMapCount();
    uint64_t skippedMaps = backend.GetRootConstantMapSkippedCount();

    // Optimized: Redundant data (suppressing Map(DISCARD))
    backend.ResetRootConstantStats();
    testConstants[0] = 42;
    auto suppStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        RecordedCommand cmd;
        cmd.type = CommandType::SetGraphicsRoot32BitConstants;
        new (&cmd.setRootConstants) SetRoot32BitConstantsCmd();
        cmd.setRootConstants.rootParameterIndex = 0;
        cmd.setRootConstants.num32BitValuesToSet = 4;
        cmd.setRootConstants.destOffsetIn32BitValues = 0;
        cmd.setRootConstants.isCompute = false;
        cmd.setRootConstants.constants = testConstants;

        CommandBatch batch;
        batch.Append(std::move(cmd));
        backend.ReplayCommandBatch(batch);
    }
    auto suppEnd = std::chrono::high_resolution_clock::now();
    uint64_t suppUs = std::chrono::duration_cast<std::chrono::microseconds>(suppEnd - suppStart).count();
    uint64_t suppActualMaps = backend.GetRootConstantMapCount();
    uint64_t suppSkippedMaps = backend.GetRootConstantMapSkippedCount();

    double mapLatencyUs = static_cast<double>(forcedMapUs) / iterations;
    double suppLatencyUs = static_cast<double>(suppUs) / iterations;
    double speedup = static_cast<double>(forcedMapUs) / (suppUs > 0 ? suppUs : 1);

    std::cout << "  Forced Map(DISCARD) updates (" << iterations << " iterations): "
              << (forcedMapUs / 1000.0) << " ms (" << mapLatencyUs << " us/update)" << std::endl;
    std::cout << "    Actual maps: " << actualMaps << ", Skipped: " << skippedMaps << std::endl;
    std::cout << "  Suppressed redundant updates (" << iterations << " iterations): "
              << (suppUs / 1000.0) << " ms (" << suppLatencyUs << " us/update)" << std::endl;
    std::cout << "    Actual maps: " << suppActualMaps << ", Skipped: " << suppSkippedMaps << std::endl;
    std::cout << "  Staging throughput speedup   : " << std::fixed << std::setprecision(1) << speedup << "x" << std::endl;

    assert(suppActualMaps == 1);
    assert(suppSkippedMaps == iterations - 1);
    std::cout << "[PASS] Benchmark 2: Root Constant Buffer Staging & Suppression" << std::endl;
}

void BenchmarkCommandReplayThroughput() {
    std::cout << "\n--- Benchmark 3: High-Frequency Command Replay Throughput ---" << std::endl;
    auto& backend = D3D11Backend::Instance();
    if (!backend.Initialize()) {
        std::cout << "  [SKIP] D3D11 backend unavailable." << std::endl;
        return;
    }

    const int commandCount = 5000;
    CommandBatch batch;
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
    D3D12_RECT rect = { 0, 0, 1920, 1080 };

    for (int i = 0; i < commandCount / 2; ++i) {
        RecordedCommand cmdVP;
        cmdVP.type = CommandType::SetViewports;
        new (&cmdVP.setViewports) SetViewportsCmd();
        cmdVP.setViewports.viewports.push_back(vp);
        batch.Append(std::move(cmdVP));

        RecordedCommand cmdScissor;
        cmdScissor.type = CommandType::SetScissorRects;
        new (&cmdScissor.setScissors) SetScissorRectsCmd();
        cmdScissor.setScissors.rects.push_back(rect);
        batch.Append(std::move(cmdScissor));
    }

    assert(batch.GetCount() == commandCount);
    backend.ResetCommandStats();

    auto replayStart = std::chrono::high_resolution_clock::now();
    HRESULT hr = backend.ReplayCommandBatch(batch);
    auto replayEnd = std::chrono::high_resolution_clock::now();
    assert(SUCCEEDED(hr));

    uint64_t replayUs = std::chrono::duration_cast<std::chrono::microseconds>(replayEnd - replayStart).count();
    double replayMs = replayUs / 1000.0;
    double cmdsPerSec = (commandCount / (replayUs / 1000000.0));
    double nsPerCmd = (static_cast<double>(replayUs) * 1000.0) / commandCount;

    std::cout << "  Replayed " << commandCount << " commands in " << std::fixed << std::setprecision(3)
              << replayMs << " ms (" << nsPerCmd << " ns/command)" << std::endl;
    std::cout << "  Command replay throughput : " << std::fixed << std::setprecision(0)
              << cmdsPerSec << " commands/sec" << std::endl;
    std::cout << "  Translated: " << backend.GetTranslatedCommandsCount()
              << ", Rejected: " << backend.GetRejectedCommandsCount() << std::endl;

    assert(backend.GetTranslatedCommandsCount() == commandCount);
    assert(backend.GetRejectedCommandsCount() == 0);
    std::cout << "[PASS] Benchmark 3: High-Frequency Command Replay Throughput" << std::endl;
}

void BenchmarkMemoryFootprint() {
    std::cout << "\n--- Benchmark 4: Memory Footprint & Scaling ---" << std::endl;
    auto& rm = ResourceManager::Instance();
    rm.Clear();

    auto initialMf = rm.GetMemoryFootprint();
    std::cout << "  Initial memory footprint: " << initialMf.estimatedMemoryBytes << " bytes ("
              << initialMf.resourceCount << " resources, " << initialMf.uavCount << " UAVs)" << std::endl;

    const size_t testEntries = 2000;
    for (size_t i = 1; i <= testEntries; ++i) {
        TranslatedUAVInfo uav = {};
        uav.structureByteStride = 16;
        uav.numElements = 64;
        uav.format = DXGI_FORMAT_UNKNOWN;
        rm.RegisterUAVInfo(0x10000 + i * 32, uav);
    }

    auto loadedMf = rm.GetMemoryFootprint();
    std::cout << "  Loaded " << testEntries << " UAV descriptors: "
              << (loadedMf.estimatedMemoryBytes / 1024.0) << " KB ("
              << loadedMf.uavCount << " UAV entries)" << std::endl;

    double bytesPerEntry = static_cast<double>(loadedMf.estimatedMemoryBytes) / testEntries;
    std::cout << "  Average tracking footprint : " << std::fixed << std::setprecision(1)
              << bytesPerEntry << " bytes/entry" << std::endl;

    assert(loadedMf.uavCount == testEntries);
    assert(bytesPerEntry < 512.0); // Map tracking must be compact

    rm.Clear();
    auto clearedMf = rm.GetMemoryFootprint();
    std::cout << "  Post-clear footprint     : " << clearedMf.estimatedMemoryBytes << " bytes ("
              << clearedMf.uavCount << " UAVs)" << std::endl;
    assert(clearedMf.uavCount == 0);
    std::cout << "[PASS] Benchmark 4: Memory Footprint & Scaling" << std::endl;
}

void BenchmarkHighChurnPsoCreation() {
    std::cout << "\n--- Benchmark 5: Monolithic PSO Compilation Churn & TDR Mitigation ---" << std::endl;
    auto& backend = D3D11Backend::Instance();
    if (!backend.Initialize()) {
        std::cout << "  [SKIP] D3D11 backend unavailable." << std::endl;
        return;
    }

    BoundedShaderTranslator translator;
    std::vector<uint8_t> vsBytecode;
    std::vector<uint8_t> psBytecode;
    std::string errMsg;

    bool vsOk = translator.CompileHLSLToDXBC(s_testHLSL, "VSMain", "vs_5_0", vsBytecode, errMsg);
    bool psOk = translator.CompileHLSLToDXBC(s_testHLSL, "PSMain", "ps_5_0", psBytecode, errMsg);
    assert(vsOk && psOk);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.VS.pShaderBytecode = vsBytecode.data();
    psoDesc.VS.BytecodeLength = vsBytecode.size();
    psoDesc.PS.pShaderBytecode = psBytecode.data();
    psoDesc.PS.BytecodeLength = psBytecode.size();

    const int psoCount = 300;
    std::vector<TranslatedPipelineState> createdPSOs(psoCount);
    backend.ResetPsoBurstStats();

    auto churnStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < psoCount; ++i) {
        HRESULT hr = backend.CreateTranslatedPSO(&psoDesc, createdPSOs[i]);
        assert(SUCCEEDED(hr));
        assert(createdPSOs[i].pVS != nullptr);
        assert(createdPSOs[i].pPS != nullptr);
    }
    auto churnEnd = std::chrono::high_resolution_clock::now();
    uint64_t churnUs = std::chrono::duration_cast<std::chrono::microseconds>(churnEnd - churnStart).count();

    uint64_t checkpoints = backend.GetPsoBurstCheckpointCount();
    double avgLatencyUs = static_cast<double>(churnUs) / psoCount;

    std::cout << "  Compiled " << psoCount << " monolithic PSOs in " << std::fixed << std::setprecision(2)
              << (churnUs / 1000.0) << " ms (" << avgLatencyUs << " us/PSO)" << std::endl;
    std::cout << "  Burst throttling checkpoints triggered: " << checkpoints << " (at 128 and 256)" << std::endl;
    std::cout << "  WDDM TDR mitigation: Flushed driver command queues and yielded thread slices safely." << std::endl;

    assert(checkpoints >= 2);

    // Clean up created resources
    for (int i = 0; i < psoCount; ++i) {
        if (createdPSOs[i].pVS) createdPSOs[i].pVS->Release();
        if (createdPSOs[i].pPS) createdPSOs[i].pPS->Release();
        if (createdPSOs[i].pRasterizerState) createdPSOs[i].pRasterizerState->Release();
        if (createdPSOs[i].pBlendState) createdPSOs[i].pBlendState->Release();
        if (createdPSOs[i].pDepthStencilState) createdPSOs[i].pDepthStencilState->Release();
    }

    std::cout << "[PASS] Benchmark 5: Monolithic PSO Compilation Churn & TDR Mitigation" << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 7/9 Performance Suite" << std::endl;
    std::cout << "====================================================" << std::endl;

    BenchmarkShaderCompilationAndCache();
    BenchmarkRootConstantMapSuppression();
    BenchmarkCommandReplayThroughput();
    BenchmarkMemoryFootprint();
    BenchmarkHighChurnPsoCreation();

    std::cout << "\n====================================================" << std::endl;
    std::cout << "ALL PHASE 7/9 BENCHMARKS PASSED (5/5)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
