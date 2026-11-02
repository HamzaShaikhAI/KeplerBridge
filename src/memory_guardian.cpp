#include "translator/memory_guardian.h"
#include "core/logger.h"
#include <sstream>
#include <iomanip>

namespace kepler {

MemoryGuardian& MemoryGuardian::Instance() {
    static MemoryGuardian s_instance;
    return s_instance;
}

MemoryGuardian::MemoryGuardian() {
}

void MemoryGuardian::SetSoftBudgetMB(size_t budgetMB) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_softBudgetMB = budgetMB;
}

size_t MemoryGuardian::GetSoftBudgetMB() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_softBudgetMB;
}

void MemoryGuardian::RegisterAllocation(
    uint64_t id,
    const std::string& name,
    size_t sizeBytes,
    bool isTexture,
    bool isBuffer,
    uint32_t currentFrame)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    TrackedAllocation alloc{};
    alloc.id = id;
    alloc.name = name;
    alloc.sizeBytes = sizeBytes;
    alloc.isTexture = isTexture;
    alloc.isBuffer = isBuffer;
    alloc.creationFrame = currentFrame;
    alloc.lastUsedFrame = currentFrame;

    m_allocations[id] = alloc;
    size_t cur = m_currentTrackedBytes.fetch_add(sizeBytes) + sizeBytes;
    m_activeResourceCount.fetch_add(1);

    size_t peak = m_peakTrackedBytes.load();
    while (cur > peak && !m_peakTrackedBytes.compare_exchange_weak(peak, cur)) {}

    // Check budget warnings
    size_t budgetBytes = m_softBudgetMB * 1024 * 1024;
    if (cur > budgetBytes) {
        LOG_WARN("[MemoryGuardian] Soft budget exceeded: " + std::to_string(cur / (1024 * 1024)) +
                 " MB allocated (Budget: " + std::to_string(m_softBudgetMB) + " MB)");
    }
}

void MemoryGuardian::UnregisterAllocation(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(id);
    if (it != m_allocations.end()) {
        size_t size = it->second.sizeBytes;
        m_allocations.erase(it);
        if (m_currentTrackedBytes.load() >= size) {
            m_currentTrackedBytes.fetch_sub(size);
        } else {
            m_currentTrackedBytes.store(0);
        }
        if (m_activeResourceCount.load() > 0) {
            m_activeResourceCount.fetch_sub(1);
        }
    }
}

void MemoryGuardian::TouchAllocation(uint64_t id, uint32_t currentFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(id);
    if (it != m_allocations.end()) {
        it->second.lastUsedFrame = currentFrame;
    }
}

void MemoryGuardian::RecordAllocationFailure(size_t requestedBytes, const std::string& reason) {
    m_allocationFailures.fetch_add(1);
    LOG_ERROR("[MemoryGuardian] Allocation failure for " + std::to_string(requestedBytes) +
              " bytes: " + reason);
}

void MemoryGuardian::RecordDeviceRemoved(HRESULT reason) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastDeviceRemovalReason = reason;
    LOG_ERROR("[MemoryGuardian] Device removal recorded with HRESULT 0x" +
              [&]() { std::stringstream ss; ss << std::hex << reason; return ss.str(); }());
}

bool MemoryGuardian::QueryHardwareMemoryInfo(IUnknown* pAdapter, AdapterMemoryInfo& outInfo) {
    outInfo = {};
    if (!pAdapter) return false;

    IDXGIAdapter3* pAdapter3 = nullptr;
    HRESULT hr = pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter3));
    if (FAILED(hr) || !pAdapter3) {
        return false;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO vmi{};
    hr = pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmi);
    pAdapter3->Release();

    if (SUCCEEDED(hr)) {
        outInfo.querySucceeded = true;
        outInfo.budgetBytes = vmi.Budget;
        outInfo.currentUsageBytes = vmi.CurrentUsage;
        outInfo.availableForReservation = vmi.AvailableForReservation;
        outInfo.currentReservation = vmi.CurrentReservation;
        return true;
    }
    return false;
}

MemoryPressureLevel MemoryGuardian::GetPressureLevel() const {
    size_t cur = m_currentTrackedBytes.load();
    size_t budgetBytes;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        budgetBytes = m_softBudgetMB * 1024 * 1024;
    }

    if (budgetBytes == 0) return MemoryPressureLevel::Normal;
    double ratio = static_cast<double>(cur) / static_cast<double>(budgetBytes);
    if (ratio >= 0.95 || m_allocationFailures.load() > 0) {
        return MemoryPressureLevel::Critical;
    } else if (ratio >= 0.80) {
        return MemoryPressureLevel::Warning;
    }
    return MemoryPressureLevel::Normal;
}

std::vector<TrackedAllocation> MemoryGuardian::GetActiveAllocations() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<TrackedAllocation> res;
    res.reserve(m_allocations.size());
    for (const auto& pair : m_allocations) {
        res.push_back(pair.second);
    }
    return res;
}

std::vector<TrackedAllocation> MemoryGuardian::DetectPotentialLeaks(uint32_t currentFrame, uint32_t ageThresholdFrames) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<TrackedAllocation> leaks;
    for (const auto& pair : m_allocations) {
        if (currentFrame > pair.second.lastUsedFrame &&
            (currentFrame - pair.second.lastUsedFrame) >= ageThresholdFrames)
        {
            leaks.push_back(pair.second);
        }
    }
    return leaks;
}

std::string MemoryGuardian::GetDiagnosticsReport() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss << "=== KeplerBridge Memory Guardian Report ===\n";
    ss << "Soft Budget     : " << m_softBudgetMB << " MB\n";
    ss << "Current Tracked : " << (m_currentTrackedBytes.load() / (1024 * 1024)) << " MB ("
       << m_currentTrackedBytes.load() << " bytes)\n";
    ss << "Peak Tracked    : " << (m_peakTrackedBytes.load() / (1024 * 1024)) << " MB ("
       << m_peakTrackedBytes.load() << " bytes)\n";
    ss << "Active Resources: " << m_allocations.size() << "\n";
    ss << "Failures Count  : " << m_allocationFailures.load() << "\n";
    ss << "Last Device Rem : 0x" << std::hex << m_lastDeviceRemovalReason << std::dec << "\n";
    ss << "===========================================\n";
    return ss.str();
}

void MemoryGuardian::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocations.clear();
    m_currentTrackedBytes.store(0);
    m_peakTrackedBytes.store(0);
    m_activeResourceCount.store(0);
    m_allocationFailures.store(0);
    m_lastDeviceRemovalReason = S_OK;
}

} // namespace kepler
