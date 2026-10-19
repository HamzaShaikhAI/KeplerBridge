#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace kepler {

struct PolicyEvaluationResult {
    bool should_intercept = false;
    D3D_FEATURE_LEVEL native_level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL requested_level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL target_spoof_level = D3D_FEATURE_LEVEL_12_0;
    D3D_FEATURE_LEVEL creation_level = D3D_FEATURE_LEVEL_11_0;
    std::string reason;
};

class FeatureLevelCompat {
public:
    static FeatureLevelCompat& Instance();

    // Pure policy evaluation (deterministic, testable in isolation without GPU)
    PolicyEvaluationResult EvaluatePolicy(
        bool spoofEnabled,
        const std::string& targetFeatureLevelStr,
        D3D_FEATURE_LEVEL nativeMaxLevel,
        D3D_FEATURE_LEVEL requestedLevel);

    // Queries native Direct3D 12 maximum feature level from the actual hardware adapter
    D3D_FEATURE_LEVEL DetectNativeMaxFeatureLevel(IUnknown* pAdapter);

    // Parses configuration string (e.g., "12_0", "12_1", "11_1", "11_0") to D3D_FEATURE_LEVEL
    static D3D_FEATURE_LEVEL ParseFeatureLevelString(const std::string& str);

    // Converts D3D_FEATURE_LEVEL to standardized short string ("11_0", "12_0", etc.)
    static std::string FeatureLevelToShortString(D3D_FEATURE_LEVEL fl);

    // Attaches compatibility hooks to a newly created native ID3D12Device
    bool InstallDeviceHooks(ID3D12Device* pDevice, D3D_FEATURE_LEVEL targetSpoofLevel);

    // Unhooks and cleans up tracked resources for an ID3D12Device
    void RemoveDeviceHooks(ID3D12Device* pDevice);

    // Hooked COM methods
    static HRESULT STDMETHODCALLTYPE HookedCheckFeatureSupport(
        ID3D12Device* This,
        D3D12_FEATURE Feature,
        void* pFeatureSupportData,
        UINT FeatureSupportDataSize);

    static ULONG STDMETHODCALLTYPE HookedRelease(ID3D12Device* This);

private:
    FeatureLevelCompat() = default;
    ~FeatureLevelCompat();
    FeatureLevelCompat(const FeatureLevelCompat&) = delete;
    FeatureLevelCompat& operator=(const FeatureLevelCompat&) = delete;

    typedef HRESULT (STDMETHODCALLTYPE *PFN_CheckFeatureSupport)(ID3D12Device*, D3D12_FEATURE, void*, UINT);
    typedef ULONG (STDMETHODCALLTYPE *PFN_Release)(ID3D12Device*);

    struct DeviceHookContext {
        void** original_vtbl = nullptr;
        void** shadow_vtbl = nullptr;
        PFN_CheckFeatureSupport real_check_feature_support = nullptr;
        PFN_Release real_release = nullptr;
        D3D_FEATURE_LEVEL spoofed_feature_level = D3D_FEATURE_LEVEL_12_0;
    };

    std::mutex m_mutex;
    std::unordered_map<ID3D12Device*, DeviceHookContext> m_hookedDevices;
};

} // namespace kepler
