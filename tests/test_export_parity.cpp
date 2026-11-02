#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] " << msg << " (Line " << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

struct ExportExpected {
    WORD ordinal;
    const char* name;
};

static const ExportExpected kExpectedExports[] = {
    { 100, "SetAppCompatStringPointer" },
    { 101, "D3D12CreateDevice" },
    { 102, "D3D12GetDebugInterface" },
    { 103, "D3D12CoreCreateLayeredDevice" },
    { 104, "D3D12CoreGetLayeredDeviceSize" },
    { 105, "D3D12CoreRegisterLayers" },
    { 106, "D3D12CreateRootSignatureDeserializer" },
    { 107, "D3D12CreateVersionedRootSignatureDeserializer" },
    { 108, "D3D12DeviceRemovedExtendedData" },
    { 109, "D3D12EnableExperimentalFeatures" },
    { 110, "D3D12GetInterface" },
    { 111, "D3D12PIXEventsReplaceBlock" },
    { 112, "D3D12PIXGetThreadInfo" },
    { 113, "D3D12PIXNotifyWakeFromFenceSignal" },
    { 114, "D3D12PIXReportCounter" },
    { 115, "D3D12SerializeRootSignature" },
    { 116, "D3D12SerializeVersionedRootSignature" },
    { 117, "GetBehaviorValue" }
};

// ----------------------------------------------------------------------------
// Test 1: In-memory PE export table parsing and ordinal-name equivalence
// ----------------------------------------------------------------------------
bool TestExportTableParity(HMODULE hDll) {
    std::cout << "[TEST 1] Verifying In-Memory Export Table Parity against Microsoft D3D12 ABI...\n";
    TEST_CHECK(hDll != nullptr, "DLL module handle is null");

    for (const auto& expected : kExpectedExports) {
        // 1. Resolve by name
        FARPROC procByName = GetProcAddress(hDll, expected.name);
        TEST_CHECK(procByName != nullptr, 
                   (std::string("Missing required named export: ") + expected.name).c_str());

        // 2. Resolve by ordinal
        FARPROC procByOrd = GetProcAddress(hDll, MAKEINTRESOURCEA(expected.ordinal));
        TEST_CHECK(procByOrd != nullptr, 
                   (std::string("Missing required ordinal export: ") + std::to_string(expected.ordinal)).c_str());

        // 3. Both must resolve to the identical entry address
        TEST_CHECK(procByName == procByOrd, 
                   (std::string("Ordinal ") + std::to_string(expected.ordinal) + 
                    " does not match address of name " + expected.name).c_str());

        std::cout << "  Ordinal " << expected.ordinal << ": " << expected.name 
                  << " -> Address 0x" << std::hex << (uintptr_t)procByName << std::dec << " [MATCH]\n";
    }

    std::cout << "  -> [PASS] All 18 Microsoft system D3D12 exports verified with 100% address parity.\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 2: PE Header parsing for ordinal continuity and duplicate prevention
// ----------------------------------------------------------------------------
bool TestExportDirectoryStructure(HMODULE hDll) {
    std::cout << "[TEST 2] Inspecting PE Export Directory Header for Continuity & Conflicts...\n";
    TEST_CHECK(hDll != nullptr, "DLL module handle is null");

    auto* base = reinterpret_cast<BYTE*>(hDll);
    auto* dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    TEST_CHECK(dosHeader->e_magic == IMAGE_DOS_SIGNATURE, "Invalid DOS signature");

    auto* ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dosHeader->e_lfanew);
    TEST_CHECK(ntHeaders->Signature == IMAGE_NT_SIGNATURE, "Invalid NT signature");

    DWORD exportRVA = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    TEST_CHECK(exportRVA != 0, "No export directory found");

    auto* exportDir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base + exportRVA);
    std::cout << "  Export Base Ordinal : " << exportDir->Base << "\n";
    std::cout << "  Number of Functions  : " << exportDir->NumberOfFunctions << "\n";
    std::cout << "  Number of Names      : " << exportDir->NumberOfNames << "\n";

    TEST_CHECK(exportDir->Base <= 100, "Export base must accommodate ordinal 100");
    TEST_CHECK(exportDir->NumberOfFunctions >= 18, "Export table must contain at least 18 functions");

    auto* addressOfFunctions = reinterpret_cast<DWORD*>(base + exportDir->AddressOfFunctions);
    auto* addressOfNames = reinterpret_cast<DWORD*>(base + exportDir->AddressOfNames);
    auto* addressOfNameOrdinals = reinterpret_cast<WORD*>(base + exportDir->AddressOfNameOrdinals);

    std::map<WORD, std::string> ordinalToName;
    for (DWORD i = 0; i < exportDir->NumberOfNames; ++i) {
        WORD ord = exportDir->Base + addressOfNameOrdinals[i];
        const char* name = reinterpret_cast<const char*>(base + addressOfNames[i]);
        TEST_CHECK(ordinalToName.find(ord) == ordinalToName.end(), "Duplicate export ordinal detected in PE directory");
        ordinalToName[ord] = name;
    }

    // Verify critical ordinals
    TEST_CHECK(ordinalToName[101] == "D3D12CreateDevice", "Ordinal 101 must be D3D12CreateDevice");
    TEST_CHECK(ordinalToName[102] == "D3D12GetDebugInterface", "Ordinal 102 must be D3D12GetDebugInterface");
    TEST_CHECK(ordinalToName[110] == "D3D12GetInterface", "Ordinal 110 must be D3D12GetInterface");
    TEST_CHECK(ordinalToName[100] == "SetAppCompatStringPointer", "Ordinal 100 must be SetAppCompatStringPointer");

    std::cout << "  -> [PASS] PE export directory is strictly validated without collision or overlap.\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 3: Dependent DLL import resolution test (Streamline sl.common.dll)
// ----------------------------------------------------------------------------
bool TestDependentModuleImportResolution() {
    std::cout << "[TEST 3] Testing Dependent DLL Import Resolution (sl.common.dll)...\n";
    const wchar_t* commonPath = L"D:\\Program Files\\Palworld\\Palworld\\Pal\\Plugins\\StreamlineCore\\Binaries\\ThirdParty\\Win64\\sl.common.dll";

    // Suppress modal error popups during diagnostic query
    DWORD oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    HMODULE hCommon = LoadLibraryW(commonPath);
    DWORD lastErr = GetLastError();
    SetErrorMode(oldMode);

    if (hCommon) {
        std::cout << "  Successfully loaded sl.common.dll with KeplerBridge proxy in memory! Handle: " 
                  << hCommon << ", GetLastError: 0\n";
        FreeLibrary(hCommon);
        std::cout << "  -> [PASS] sl.common.dll resolved Ordinal 101 without ERROR_INVALID_ORDINAL (182).\n";
        return true;
    } else {
        std::cout << "  Note: sl.common.dll was not found at standard path or could not be loaded. Error: " 
                  << lastErr << "\n";
        // If file does not exist on disk, do not fail unit test suite on non-Palworld test systems
        if (GetFileAttributesW(commonPath) == INVALID_FILE_ATTRIBUTES) {
            std::cout << "  -> [SKIP] File does not exist at path; skipped standalone plugin probe.\n";
            return true;
        }
        TEST_CHECK(lastErr != ERROR_INVALID_ORDINAL, "Failed with ERROR_INVALID_ORDINAL (182)!");
        return false;
    }
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Phase 11E D3D12 Export Parity Test Suite \n";
    std::cout << "========================================================\n";

    HMODULE hD3D12 = LoadLibraryA(".\\d3d12.dll");
    if (!hD3D12) {
        hD3D12 = LoadLibraryA("d3d12.dll");
    }
    if (!hD3D12) {
        std::cerr << "[FATAL] Failed to load candidate d3d12.dll!\n";
        return 1;
    }

    int passed = 0;
    int failed = 0;

    if (TestExportTableParity(hD3D12)) passed++; else failed++;
    if (TestExportDirectoryStructure(hD3D12)) passed++; else failed++;
    if (TestDependentModuleImportResolution()) passed++; else failed++;

    FreeLibrary(hD3D12);

    std::cout << "========================================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed.\n";
    std::cout << "========================================================\n";

    return (failed == 0) ? 0 : 1;
}
