#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/logger.h"
#include "core/config.h"
#include "core/proxy_loader.h"

#include <string>

static std::string GetDirectoryFromPath(const std::string& path) {
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash);
    }
    return ".";
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);

            // Determine host executable directory
            char exePath[MAX_PATH] = { 0 };
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            std::string exeDir = GetDirectoryFromPath(exePath);

            // Determine proxy DLL directory
            char dllPath[MAX_PATH] = { 0 };
            GetModuleFileNameA(hModule, dllPath, MAX_PATH);
            std::string dllDir = GetDirectoryFromPath(dllPath);

            // Initialize logger in host executable directory
            std::string logFilePath = exeDir + "\\kepler_bridge.log";
            kepler::Logger::Instance().Initialize(logFilePath);

            LOG_RAW("================================================================================");
            LOG_RAW("  KeplerBridge Phase 1: The Ghost Hook (DLL Proxy & Telemetry Logger)");
            LOG_RAW("  Architecture: x64 | Target: NVIDIA Kepler GT 740 D3D12->D3D11 Bridge");
            LOG_RAW("================================================================================");

            LOG_INFO("DLL_PROCESS_ATTACH received.");
            LOG_INFO("Host Executable: " + std::string(exePath));
            LOG_INFO("KeplerBridge DLL: " + std::string(dllPath));
            LOG_INFO("Logging initialized at: " + logFilePath);

            // Locate and load kepler_bridge.ini
            std::string iniPath = exeDir + "\\kepler_bridge.ini";
            if (GetFileAttributesA(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                // Check DLL directory if not in exe dir
                std::string altIniPath = dllDir + "\\kepler_bridge.ini";
                if (GetFileAttributesA(altIniPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    iniPath = altIniPath;
                }
            }
            kepler::Config::Instance().Load(iniPath);
            kepler::Config::Instance().LogSettings();

            // Initialize proxy loader to hook and forward to System32\d3d12.dll
            if (!kepler::ProxyLoader::Instance().Initialize(hModule)) {
                LOG_ERROR("Failed to initialize ProxyLoader! Forwarding calls may fail.");
            } else {
                LOG_INFO("ProxyLoader initialized successfully. System hooks active.");
            }
            break;
        }

        case DLL_PROCESS_DETACH: {
            LOG_INFO("DLL_PROCESS_DETACH received. Initiating graceful shutdown.");
            kepler::ProxyLoader::Instance().Shutdown();
            LOG_INFO("KeplerBridge unloaded successfully.");
            kepler::Logger::Instance().Shutdown();
            break;
        }

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}
