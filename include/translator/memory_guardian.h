#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace kepler {

enum class MemoryPressureLevel {
    Normal,
    Warning,   // Approaching soft budget (>80%)
    Critical   // Exceeds soft budget (>95%) or allocation failed
};

struct TrackedAllocation {
    uint64_t id = 0;
    std::string name;
    size_t sizeBytes = 0;
    bool isTexture = false;
    bool isBuffer = false;
    uint32_t creationFrame = 0;
    uint32_t lastUsedFrame = 0;
};

struct AdapterMemoryInfo {
    bool querySucceeded = false;
    uint64_t budgetBytes = 0;
    uint64_t currentUsageBytes = 0;
    uint64_t availableForReservation = 0;
    uint64_t currentReservation = 0;
};

class MemoryGuardian {
public:
    static MemoryGuardian& Instance();

    void SetSoftBudgetMB(size_t budgetMB);
    size_t GetSoftBudgetMB() const;

    // Track life cycle of D3D12/D3D11 resources
    void RegisterAllocation(
        uint64_t id,
        const std::string& name,
        size_t sizeBytes,
        bool isTexture,
        bool isBuffer,
        uint32_t currentFrame = 0);

    void UnregisterAllocation(uint64_t id);
    void TouchAllocation(uint64_t id, uint32_t currentFrame);

    void RecordAllocationFailure(size_t requestedBytes, const std::string& reason);
    void RecordDeviceRemoved(HRESULT reason);

    // Queries actual DXGI 1.4 video memory info if adapter supports IDXGIAdapter3
    bool QueryHardwareMemoryInfo(IUnknown* pAdapter, AdapterMemoryInfo& outInfo);

    MemoryPressureLevel GetPressureLevel() const;

    // Reporting & Leaks
    size_t GetCurrentTrackedBytes() const { return m_currentTrackedBytes.load(); }
    size_t GetPeakTrackedBytes() const { return m_peakTrackedBytes.load(); }
    uint64_t GetActiveResourceCount() const { return m_activeResourceCount.load(); }
    uint64_t GetAllocationFailureCount() const { return m_allocationFailures.load(); }

    std::vector<TrackedAllocation> GetActiveAllocations() const;
    std::vector<TrackedAllocation> DetectPotentialLeaks(uint32_t currentFrame, uint32_t ageThresholdFrames = 300) const;

    std::string GetDiagnosticsReport() const;
    void Reset();

private:
    MemoryGuardian();
    ~MemoryGuardian() = default;
    MemoryGuardian(const MemoryGuardian&) = delete;
    MemoryGuardian& operator=(const MemoryGuardian&) = delete;

    mutable std::mutex m_mutex;
    size_t m_softBudgetMB = 3072; // Default 3072 MB (3 GB for 4GB GT 740)

    std::atomic<size_t> m_currentTrackedBytes{ 0 };
    std::atomic<size_t> m_peakTrackedBytes{ 0 };
    std::atomic<uint64_t> m_activeResourceCount{ 0 };
    std::atomic<uint64_t> m_allocationFailures{ 0 };
    HRESULT m_lastDeviceRemovalReason = S_OK;

    std::unordered_map<uint64_t, TrackedAllocation> m_allocations;
};

} // namespace kepler
