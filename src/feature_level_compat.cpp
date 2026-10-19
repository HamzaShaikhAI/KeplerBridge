#include "core/feature_level_compat.h"
#include "core/proxy_loader.h"
#include "core/logger.h"
#include "core/config.h"

#include <algorithm>
#include <sstream>

namespace kepler {

FeatureLevelCompat& FeatureLevelCompat::Instance() {
    static FeatureLevelCompat s_instance;
    return s_instance;
}

FeatureLevelCompat::~FeatureLevelCompat() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_hookedDevices) {
        if (pair.first && pair.second.original_vtbl) {
            *(void***)pair.first = pair.second.original_vtbl;
        }
        if (pair.second.shadow_vtbl) {
            delete[] pair.second.shadow_vtbl;
        }
    }
    m_hookedDevices.clear();
}

D3D_FEATURE_LEVEL FeatureLevelCompat::ParseFeatureLevelString(const std::string& str) {
    if (str == "12_2" || str == "12.2" || str == "0xc200" || str == "0XC200") {
        return D3D_FEATURE_LEVEL_12_2;
    }
    if (str == "12_1" || str == "12.1" || str == "0xc100" || str == "0XC100") {
        return D3D_FEATURE_LEVEL_12_1;
    }
    if (str == "12_0" || str == "12.0" || str == "0xc000" || str == "0XC000") {
        return D3D_FEATURE_LEVEL_12_0;
    }
    if (str == "11_1" || str == "11.1" || str == "0xb100" || str == "0XB100") {
        return D3D_FEATURE_LEVEL_11_1;
    }
    if (str == "11_0" || str == "11.0" || str == "0xb000" || str == "0XB000") {
        return D3D_FEATURE_LEVEL_11_0;
    }
    // Default fallback
    return D3D_FEATURE_LEVEL_12_0;
}

std::string FeatureLevelCompat::FeatureLevelToShortString(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
        case D3D_FEATURE_LEVEL_12_2: return "12_2";
        case D3D_FEATURE_LEVEL_12_1: return "12_1";
        case D3D_FEATURE_LEVEL_12_0: return "12_0";
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        case D3D_FEATURE_LEVEL_9_3:  return "9_3";
        case D3D_FEATURE_LEVEL_9_2:  return "9_2";
        case D3D_FEATURE_LEVEL_9_1:  return "9_1";
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%X", (unsigned int)fl);
            return std::string(buf);
        }
    }
}

PolicyEvaluationResult FeatureLevelCompat::EvaluatePolicy(
    bool spoofEnabled,
    const std::string& targetFeatureLevelStr,
    D3D_FEATURE_LEVEL nativeMaxLevel,
    D3D_FEATURE_LEVEL requestedLevel)
{
    PolicyEvaluationResult result;
    result.native_level = nativeMaxLevel;
    result.requested_level = requestedLevel;
    result.target_spoof_level = ParseFeatureLevelString(targetFeatureLevelStr);

    if (!spoofEnabled) {
        result.should_intercept = false;
        result.creation_level = requestedLevel;
        result.reason = "Compatibility spoofing is disabled in kepler_bridge.ini ([Spoof] EnableFeatureLevelSpoof=0).";
        return result;
    }

    if (requestedLevel <= nativeMaxLevel) {
        result.should_intercept = false;
        result.creation_level = requestedLevel;
        result.reason = "Requested feature level (" + FeatureLevelToShortString(requestedLevel) +
                        ") is supported natively by hardware (" + FeatureLevelToShortString(nativeMaxLevel) + ").";
        return result;
    }

    // Interception active
    result.should_intercept = true;
    result.creation_level = nativeMaxLevel;
    result.reason = "Requested feature level (" + FeatureLevelToShortString(requestedLevel) +
                    ") exceeds native hardware capability (" + FeatureLevelToShortString(nativeMaxLevel) +
                    "). Intercepting device creation at native level and attaching compatibility engine.";
    return result;
}

D3D_FEATURE_LEVEL FeatureLevelCompat::DetectNativeMaxFeatureLevel(IUnknown* pAdapter) {
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12CreateDevice) {
        LOG_WARN("Cannot probe native feature level: Real D3D12CreateDevice unavailable. Assuming 11_0.");
        return D3D_FEATURE_LEVEL_11_0;
    }

    static const D3D_FEATURE_LEVEL testLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    for (auto fl : testLevels) {
        ID3D12Device* probeDevice = nullptr;
        HRESULT hr = loader.RealD3D12CreateDevice(pAdapter, fl, __uuidof(ID3D12Device), reinterpret_cast<void**>(&probeDevice));
        if (SUCCEEDED(hr) && probeDevice) {
            probeDevice->Release();
            LOG_INFO("Hardware probe detected native maximum D3D12 feature level: " + Logger::FeatureLevelToString(fl));
            return fl;
        }
    }

    LOG_WARN("Hardware probe did not accept any standard D3D12 feature levels. Defaulting to 11_0.");
    return D3D_FEATURE_LEVEL_11_0;
}

bool FeatureLevelCompat::InstallDeviceHooks(ID3D12Device* pDevice, D3D_FEATURE_LEVEL targetSpoofLevel) {
    if (!pDevice) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if already hooked
    if (m_hookedDevices.find(pDevice) != m_hookedDevices.end()) {
        return true;
    }

    void** original_vtbl = *(void***)pDevice;
    if (!original_vtbl) {
        return false;
    }

    // Allocate shadow vtable (128 entries safely covers ID3D12Device up to ID3D12Device14)
    constexpr size_t kVtblEntries = 128;
    void** shadow_vtbl = new void*[kVtblEntries];
    memcpy(shadow_vtbl, original_vtbl, sizeof(void*) * kVtblEntries);

    DeviceHookContext ctx;
    ctx.original_vtbl = original_vtbl;
    ctx.shadow_vtbl = shadow_vtbl;
    ctx.real_check_feature_support = reinterpret_cast<PFN_CheckFeatureSupport>(original_vtbl[13]);
    ctx.real_release = reinterpret_cast<PFN_Release>(original_vtbl[2]);
    ctx.spoofed_feature_level = targetSpoofLevel;

    // Hook CheckFeatureSupport (Index 13) and Release (Index 2)
    shadow_vtbl[13] = reinterpret_cast<void*>(&HookedCheckFeatureSupport);
    shadow_vtbl[2]  = reinterpret_cast<void*>(&HookedRelease);

    m_hookedDevices[pDevice] = ctx;

    // Apply shadow vtable pointer to object
    *(void***)pDevice = shadow_vtbl;

    LOG_INFO("FeatureLevelCompat: Installed compatibility hooks on ID3D12Device (" +
             std::to_string(reinterpret_cast<uintptr_t>(pDevice)) + "), Target Spoof Level: " +
             Logger::FeatureLevelToString(targetSpoofLevel));
    return true;
}

void FeatureLevelCompat::RemoveDeviceHooks(ID3D12Device* pDevice) {
    if (!pDevice) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_hookedDevices.find(pDevice);
    if (it != m_hookedDevices.end()) {
        *(void***)pDevice = it->second.original_vtbl;
        delete[] it->second.shadow_vtbl;
        m_hookedDevices.erase(it);
    }
}

HRESULT STDMETHODCALLTYPE FeatureLevelCompat::HookedCheckFeatureSupport(
    ID3D12Device* This,
    D3D12_FEATURE Feature,
    void* pFeatureSupportData,
    UINT FeatureSupportDataSize)
{
    auto& compat = FeatureLevelCompat::Instance();
    DeviceHookContext ctx;
    {
        std::lock_guard<std::mutex> lock(compat.m_mutex);
        auto it = compat.m_hookedDevices.find(This);
        if (it != compat.m_hookedDevices.end()) {
            ctx = it->second;
        }
    }

    if (!ctx.real_check_feature_support) {
        return E_FAIL;
    }

    if (Feature == D3D12_FEATURE_FEATURE_LEVELS && pFeatureSupportData &&
        FeatureSupportDataSize >= sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS))
    {
        auto* pData = reinterpret_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(pFeatureSupportData);
        D3D_FEATURE_LEVEL reportedLevel = ctx.spoofed_feature_level;

        // If the game specifically asked for a set of feature levels, find the best match <= spoofed level
        if (pData->pFeatureLevelsRequested && pData->NumFeatureLevels > 0) {
            D3D_FEATURE_LEVEL bestMatch = D3D_FEATURE_LEVEL_11_0;
            bool foundAny = false;
            for (UINT i = 0; i < pData->NumFeatureLevels; ++i) {
                D3D_FEATURE_LEVEL req = pData->pFeatureLevelsRequested[i];
                if (req <= ctx.spoofed_feature_level) {
                    if (!foundAny || req > bestMatch) {
                        bestMatch = req;
                        foundAny = true;
                    }
                }
            }
            if (foundAny) {
                reportedLevel = bestMatch;
            }
        }

        pData->MaxSupportedFeatureLevel = reportedLevel;
        LOG_HOOK("[COMPAT] Spoofed CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS) -> MaxSupportedFeatureLevel = " +
                 Logger::FeatureLevelToString(reportedLevel) + " (Native hardware is 11_0)");
        return S_OK;
    }

    // For all other features (Architecture, Shader Models, Format Support, Options), delegate to native driver
    return ctx.real_check_feature_support(This, Feature, pFeatureSupportData, FeatureSupportDataSize);
}

ULONG STDMETHODCALLTYPE FeatureLevelCompat::HookedRelease(ID3D12Device* This) {
    auto& compat = FeatureLevelCompat::Instance();
    PFN_Release realRelease = nullptr;
    void** shadowVtbl = nullptr;

    {
        std::lock_guard<std::mutex> lock(compat.m_mutex);
        auto it = compat.m_hookedDevices.find(This);
        if (it != compat.m_hookedDevices.end()) {
            realRelease = it->second.real_release;
            shadowVtbl = it->second.shadow_vtbl;
        }
    }

    if (!realRelease) {
        return 0;
    }

    ULONG refCount = realRelease(This);
    if (refCount == 0) {
        // Device destroyed; unregister from map and delete shadow vtable
        std::lock_guard<std::mutex> lock(compat.m_mutex);
        auto it = compat.m_hookedDevices.find(This);
        if (it != compat.m_hookedDevices.end()) {
            delete[] it->second.shadow_vtbl;
            compat.m_hookedDevices.erase(it);
        }
        LOG_INFO("FeatureLevelCompat: Hooked ID3D12Device released to refcount 0 and cleaned up.");
    }

    return refCount;
}

} // namespace kepler
