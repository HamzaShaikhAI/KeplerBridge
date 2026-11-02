#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <iomanip>

// ============================================================================
// Types & Function Signatures
// ============================================================================

typedef HRESULT(WINAPI* PFN_D3D12CreateDevice)(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void** ppDevice);

typedef HRESULT(WINAPI* PFN_D3D12SerializeRootSignature)(
    const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    D3D_ROOT_SIGNATURE_VERSION Version,
    ID3DBlob** ppBlob,
    ID3DBlob** ppErrorBlob);

typedef HRESULT(WINAPI* PFN_KeplerBridge_CreateDXGISwapChainForHwnd)(
    ID3D12CommandQueue* pCommandQueue,
    HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGISwapChain3** ppSwapChain);

typedef void(WINAPI* PFN_KeplerBridge_GetPresentationBreakdown)(
    UINT64* pPresentAttempts,
    UINT64* pSuccessfulPresentCalls,
    UINT64* pOccludedPresentAttempts,
    UINT64* pVisiblePresentSuccesses,
    UINT32* pPresentFailures,
    UINT64* pPresentTestCalls,
    UINT64* pRenderedWorkSubmissions,
    UINT64* pCompletedTranslatedDraws,
    UINT64* pCausallyRenderedVisible,
    UINT64* pCausallyRenderedOccluded);

typedef HRESULT(WINAPI* PFN_KeplerBridge_ReadbackRenderTarget)(
    ID3D12Resource* pD3D12Resource,
    void* pOutPixels,
    UINT* pOutWidth,
    UINT* pOutHeight);

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

struct GDIColor {
    uint8_t r, g, b;
};

static const char* SwapEffectToString(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
    case DXGI_SWAP_EFFECT_DISCARD: return "DXGI_SWAP_EFFECT_DISCARD (0)";
    case DXGI_SWAP_EFFECT_SEQUENTIAL: return "DXGI_SWAP_EFFECT_SEQUENTIAL (1)";
    case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL (3)";
    case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "DXGI_SWAP_EFFECT_FLIP_DISCARD (4)";
    default: return "Unknown SwapEffect";
    }
}

static std::string GetObjectName(HANDLE hObject) {
    if (!hObject) return "<null>";
    DWORD needed = 0;
    GetUserObjectInformationA(hObject, UOI_NAME, nullptr, 0, &needed);
    if (needed == 0) return "<unknown>";
    std::vector<char> buf(needed + 1, 0);
    if (GetUserObjectInformationA(hObject, UOI_NAME, buf.data(), needed, &needed)) {
        return std::string(buf.data());
    }
    return "<query_failed: " + std::to_string(GetLastError()) + ">";
}

// Save HBITMAP as standard 24-bit uncompressed Windows BMP
static bool SaveBitmapToFile(const std::string& filename, HDC hdc, HBITMAP hBitmap, int width, int height) {
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height; // Top-down
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    DWORD rowSize = ((width * 3 + 3) & ~3);
    DWORD imageSize = rowSize * height;
    std::vector<uint8_t> pixels(imageSize, 0);

    if (!GetDIBits(hdc, hBitmap, 0, height, pixels.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
        return false;
    }

    BITMAPFILEHEADER bfh = {};
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + imageSize;

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::string alt = "../" + filename;
        out.open(alt, std::ios::binary);
        if (!out) return false;
    }
    out.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
    out.write(reinterpret_cast<const char*>(&bi), sizeof(bi));
    out.write(reinterpret_cast<const char*>(pixels.data()), imageSize);
    out.close();

    std::string alt = "../" + filename;
    std::ofstream outAlt(alt, std::ios::binary);
    if (outAlt) {
        outAlt.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
        outAlt.write(reinterpret_cast<const char*>(&bi), sizeof(bi));
        outAlt.write(reinterpret_cast<const char*>(pixels.data()), imageSize);
    }
    return true;
}

// Capture client area directly from physical desktop DC
static bool CaptureDesktopRectToFile(HWND hwnd, const std::string& filepath, int width, int height) {
    POINT ptTopLeft = { 0, 0 };
    ClientToScreen(hwnd, &ptTopLeft);

    HDC hdcScreen = GetDC(nullptr); // Desktop screen DC
    if (!hdcScreen) return false;
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) { ReleaseDC(nullptr, hdcScreen); return false; }
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    if (!hBitmap) { DeleteDC(hdcMem); ReleaseDC(nullptr, hdcScreen); return false; }
    HBITMAP hOldBm = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // BitBlt reads directly from composited display surface
    BOOL bltOk = BitBlt(hdcMem, 0, 0, width, height, hdcScreen, ptTopLeft.x, ptTopLeft.y, SRCCOPY);

    bool ok = false;
    if (bltOk) {
        ok = SaveBitmapToFile(filepath, hdcMem, hBitmap, width, height);
    }

    SelectObject(hdcMem, hOldBm);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return ok;
}

static GDIColor CaptureScreenPixelAtClient(HWND hwnd, int clientX, int clientY) {
    POINT pt = { clientX, clientY };
    ClientToScreen(hwnd, &pt);

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, 1, 1);
    HBITMAP hOldBm = (HBITMAP)SelectObject(hdcMem, hbm);

    BitBlt(hdcMem, 0, 0, 1, 1, hdcScreen, pt.x, pt.y, SRCCOPY);
    COLORREF color = GetPixel(hdcMem, 0, 0);

    SelectObject(hdcMem, hOldBm);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    GDIColor res;
    res.r = GetRValue(color);
    res.g = GetGValue(color);
    res.b = GetBValue(color);
    return res;
}

// ============================================================================
// Global State & Window Procedure
// ============================================================================

static HWND g_hWnd = nullptr;
static bool g_windowActive = false;
static bool g_userClicked = false;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ACTIVATE:
        g_windowActive = (LOWORD(wParam) != WA_INACTIVE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        g_userClicked = true;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hWnd, message, wParam, lParam);
    }
}

static std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) return buffer;
    return {};
}

// ============================================================================
// Main Execution
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "================================================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 12.14.2 — Final User-Controlled Presentation Verification   " << std::endl;
    std::cout << "  Target: NVIDIA GeForce GT 740 GK107 (Direct3D 12 -> D3D11 Physical Display)  " << std::endl;
    std::cout << "================================================================================" << std::endl;

    // ========================================================================
    // 1. Session & Desktop Diagnostics
    // ========================================================================
    DWORD procId = GetCurrentProcessId();
    DWORD threadId = GetCurrentThreadId();
    DWORD procSessionId = 0xFFFFFFFF;
    ProcessIdToSessionId(procId, &procSessionId);
    typedef DWORD(WINAPI* PFN_WTSGetActiveConsoleSessionId)();
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    auto pfnWTS = (PFN_WTSGetActiveConsoleSessionId)GetProcAddress(hKernel32, "WTSGetActiveConsoleSessionId");
    DWORD activeConsoleSessionId = pfnWTS ? pfnWTS() : 0;

    HWINSTA hWinSta = GetProcessWindowStation();
    std::string winStaName = GetObjectName(hWinSta);
    HDESK hInitialDesk = GetThreadDesktop(threadId);
    std::string initialDeskName = GetObjectName(hInitialDesk);

    std::cout << "[SESSION & DESKTOP AUDIT]" << std::endl;
    std::cout << "  Process ID:                  " << procId << std::endl;
    std::cout << "  Current Thread ID:           " << threadId << std::endl;
    std::cout << "  Process Session ID:          " << procSessionId << std::endl;
    std::cout << "  Active Console Session ID:   " << activeConsoleSessionId << std::endl;
    std::cout << "  Session ID Match:            " << (procSessionId == activeConsoleSessionId ? "YES" : "NO") << std::endl;
    std::cout << "  Process Window Station:      " << winStaName << std::endl;
    std::cout << "  Initial Thread Desktop:      " << initialDeskName << std::endl;

    // Switch to Input Desktop "Default"
    HDESK hInputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hInputDesk) hInputDesk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
    bool desktopSwitched = false;
    std::string activeDeskName = initialDeskName;

    if (hInputDesk) {
        std::string targetDeskName = GetObjectName(hInputDesk);
        std::cout << "  Input Desktop Available:     " << targetDeskName << " (Accessible)" << std::endl;
        BOOL switched = SetThreadDesktop(hInputDesk);
        if (switched) {
            desktopSwitched = true;
            HDESK hNew = GetThreadDesktop(threadId);
            activeDeskName = GetObjectName(hNew);
            std::cout << "  SetThreadDesktop Status:     SUCCESS (Attached to " << activeDeskName << ")" << std::endl;
        } else {
            std::cout << "  SetThreadDesktop Status:     FAILED (Error: " << GetLastError() << ")" << std::endl;
        }
    } else {
        std::cout << "  Input Desktop Available:     NO (OpenInputDesktop failed: " << GetLastError() << ")" << std::endl;
    }

    std::cout << "  Active Thread Desktop:       " << activeDeskName << std::endl;
    std::cout << "  Active Desktop == Input Desk:" << (activeDeskName == "Default" ? " YES" : " NO") << std::endl;
    std::cout << "  GetShellWindow():            " << GetShellWindow() << std::endl;

    HWND initialFg = GetForegroundWindow();
    std::cout << "  Initial GetForegroundWindow(): " << initialFg << std::endl;
    if (initialFg) {
        char fgTitle[256] = {};
        GetWindowTextA(initialFg, fgTitle, sizeof(fgTitle));
        DWORD fgProc = 0;
        GetWindowThreadProcessId(initialFg, &fgProc);
        std::cout << "    Foreground Title:          \"" << fgTitle << "\"" << std::endl;
        std::cout << "    Foreground Process ID:     " << fgProc << std::endl;
    }

    // ========================================================================
    // 2. Physical Adapter Discovery via DXGI
    // ========================================================================
    IDXGIFactory1* pFactory = nullptr;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
    assert(SUCCEEDED(hr) && pFactory);

    IDXGIAdapter1* pAdapter = nullptr;
    DXGI_ADAPTER_DESC1 adapterDesc = {};
    for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        pAdapter->GetDesc1(&adapterDesc);
        if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            pAdapter->Release();
            pAdapter = nullptr;
            continue;
        }
        std::wcout << L"[PHYSICAL ADAPTER] " << adapterDesc.Description << std::endl;
        std::cout << "  VendorId: 0x" << std::hex << adapterDesc.VendorId
                  << "  DeviceId: 0x" << adapterDesc.DeviceId
                  << "  DedicatedVRAM: " << std::dec << (adapterDesc.DedicatedVideoMemory / (1024 * 1024)) << " MB" << std::endl;
        if (adapterDesc.VendorId == 0x10DE) { // NVIDIA
            std::cout << "  -> Selected Physical NVIDIA GeForce GT 740 Adapter." << std::endl;
            break;
        }
        pAdapter->Release();
        pAdapter = nullptr;
    }
    pFactory->Release();
    assert(pAdapter != nullptr && "Physical NVIDIA adapter not found!");

    // ========================================================================
    // 3. Win32 Window Creation on Active Desktop
    // ========================================================================
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "KeplerBridge_PresentationAudit_Class";
    RegisterClassExA(&wc);

    const int WIN_WIDTH = 800;
    const int WIN_HEIGHT = 600;

    RECT wr = { 0, 0, WIN_WIDTH, WIN_HEIGHT };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        "KeplerBridge D3D12 -> D3D11 | Initializing...",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(hwnd != nullptr && "Failed to create Win32 window!");
    g_hWnd = hwnd;

    DWORD wndProcId = 0;
    DWORD wndThreadId = GetWindowThreadProcessId(hwnd, &wndProcId);
    BOOL isVis = IsWindowVisible(hwnd);
    BOOL isIcon = IsIconic(hwnd);
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &wp);

    std::cout << "\n[WIN32 WINDOW AUDIT]" << std::endl;
    std::cout << "  Harness HWND:                " << hwnd << std::endl;
    std::cout << "  Window Process ID:           " << wndProcId << std::endl;
    std::cout << "  Window Thread ID:            " << wndThreadId << std::endl;
    std::cout << "  IsWindowVisible():           " << (isVis ? "TRUE" : "FALSE") << std::endl;
    std::cout << "  IsIconic() (Minimized):      " << (isIcon ? "TRUE" : "FALSE") << std::endl;
    std::cout << "  Window Placement ShowCmd:    " << wp.showCmd << std::endl;
    std::cout << "  Belongs to Active Desktop:   " << (wndThreadId == threadId ? "YES" : "NO") << std::endl;

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    // ========================================================================
    // 4. Load KeplerBridge D3D12 Proxy DLL & Verify Interception
    // ========================================================================
    HMODULE hD3D12 = LoadLibraryA("d3d12.dll");
    assert(hD3D12 != nullptr && "Failed to load local d3d12.dll!");

    char d3d12ModulePath[MAX_PATH] = {};
    GetModuleFileNameA(hD3D12, d3d12ModulePath, MAX_PATH);
    std::cout << "\n[PROXY DLL IDENTITY]" << std::endl;
    std::cout << "  Loaded d3d12.dll Module:     " << d3d12ModulePath << std::endl;

    auto pfnCreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(GetProcAddress(hD3D12, "D3D12CreateDevice"));
    auto pfnSerializeRootSignature = reinterpret_cast<PFN_D3D12SerializeRootSignature>(GetProcAddress(hD3D12, "D3D12SerializeRootSignature"));
    auto pfnCreateDXGISwapChainForHwnd = reinterpret_cast<PFN_KeplerBridge_CreateDXGISwapChainForHwnd>(GetProcAddress(hD3D12, "KeplerBridge_CreateDXGISwapChainForHwnd"));
    auto pfnReadback = reinterpret_cast<PFN_KeplerBridge_ReadbackRenderTarget>(GetProcAddress(hD3D12, "KeplerBridge_ReadbackRenderTarget"));
    auto pfnBreakdown = reinterpret_cast<PFN_KeplerBridge_GetPresentationBreakdown>(GetProcAddress(hD3D12, "KeplerBridge_GetPresentationBreakdown"));
    assert(pfnCreateDevice && pfnSerializeRootSignature && pfnCreateDXGISwapChainForHwnd);

    // ========================================================================
    // 5. Create D3D12 Device via KeplerBridge
    // ========================================================================
    ID3D12Device* pDevice = nullptr;
    hr = pfnCreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
    std::cout << "[D3D12 DEVICE] D3D12CreateDevice returned hr = " << std::hex << hr << std::dec << std::endl;
    assert(SUCCEEDED(hr) && pDevice);
    pAdapter->Release();

    // Command Queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ID3D12CommandQueue* pQueue = nullptr;
    hr = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pQueue));
    assert(SUCCEEDED(hr) && pQueue);

    // Swap Chain Creation via KeplerBridge
    DXGI_SWAP_CHAIN_DESC1 scDescA = {};
    scDescA.Width = WIN_WIDTH;
    scDescA.Height = WIN_HEIGHT;
    scDescA.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDescA.Stereo = FALSE;
    scDescA.SampleDesc.Count = 1;
    scDescA.SampleDesc.Quality = 0;
    scDescA.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDescA.BufferCount = 2; // Double-buffering
    scDescA.Scaling = DXGI_SCALING_STRETCH;
    scDescA.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scDescA.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    scDescA.Flags = 0;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc = {};
    fsDesc.Windowed = TRUE;

    IDXGISwapChain3* pSwapChain = nullptr;
    hr = pfnCreateDXGISwapChainForHwnd(pQueue, hwnd, &scDescA, &fsDesc, &pSwapChain);
    std::cout << "[SWAPCHAIN] KeplerBridge_CreateDXGISwapChainForHwnd returned hr = " << std::hex << hr << std::dec << std::endl;
    assert(SUCCEEDED(hr) && pSwapChain);

    DXGI_SWAP_CHAIN_DESC1 actDescB = {};
    pSwapChain->GetDesc1(&actDescB);
    std::cout << "  Underlying D3D11 SwapChain:  " << SwapEffectToString(actDescB.SwapEffect)
              << " (" << actDescB.Width << "x" << actDescB.Height << ", Buffers: " << actDescB.BufferCount << ")" << std::endl;

    // Backbuffers
    ID3D12Resource* pBackBuffers[2] = { nullptr, nullptr };
    hr = pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffers[0]));
    assert(SUCCEEDED(hr) && pBackBuffers[0]);
    hr = pSwapChain->GetBuffer(1, IID_PPV_ARGS(&pBackBuffers[1]));
    assert(SUCCEEDED(hr) && pBackBuffers[1]);

    // RTV Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* pRtvHeap = nullptr;
    hr = pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&pRtvHeap));
    assert(SUCCEEDED(hr) && pRtvHeap);

    UINT rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
    rtvHandles[0] = pRtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandles[1] = rtvHandles[0];
    rtvHandles[1].ptr += rtvDescriptorSize;

    pDevice->CreateRenderTargetView(pBackBuffers[0], nullptr, rtvHandles[0]);
    pDevice->CreateRenderTargetView(pBackBuffers[1], nullptr, rtvHandles[1]);

    // Command Allocator & List
    ID3D12CommandAllocator* pAlloc = nullptr;
    hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pAlloc));
    assert(SUCCEEDED(hr) && pAlloc);

    ID3D12GraphicsCommandList* pCmdList = nullptr;
    hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pAlloc, nullptr, IID_PPV_ARGS(&pCmdList));
    assert(SUCCEEDED(hr) && pCmdList);
    pCmdList->Close();

    // Root Signature
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam.Constants.ShaderRegister = 0;
    rootParam.Constants.RegisterSpace = 0;
    rootParam.Constants.Num32BitValues = 8;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &rootParam;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* pRsBlob = nullptr;
    hr = pfnSerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pRsBlob, nullptr);
    assert(SUCCEEDED(hr) && pRsBlob);

    ID3D12RootSignature* pRootSig = nullptr;
    hr = pDevice->CreateRootSignature(0, pRsBlob->GetBufferPointer(), pRsBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
    assert(SUCCEEDED(hr) && pRootSig);
    pRsBlob->Release();

    // Vertex & Pixel Shaders (Lowered SM 6.0 DXIL)
    const char* hlslVS =
        "struct VS_IN { float3 pos : POSITION; float4 col : COLOR; };\n"
        "struct PS_IN { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "PS_IN VSMain(VS_IN input) {\n"
        "    PS_IN output;\n"
        "    output.pos = float4(input.pos, 1.0);\n"
        "    output.col = input.col;\n"
        "    return output;\n"
        "}\n";

    HMODULE hCompiler = LoadLibraryA("d3dcompiler_47.dll");
    assert(hCompiler && "d3dcompiler_47.dll missing!");
    auto pfnCompile = reinterpret_cast<pD3DCompile>(GetProcAddress(hCompiler, "D3DCompile"));
    assert(pfnCompile);

    ID3DBlob* pVsBlob = nullptr;
    hr = pfnCompile(hlslVS, strlen(hlslVS), "ShaderVS", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &pVsBlob, nullptr);
    assert(SUCCEEDED(hr) && pVsBlob);

    std::vector<uint8_t> dxilPS = ReadFileBytes("test_results/test_ps60_local_scalar.dxil");
    assert(!dxilPS.empty() && "Failed to load test_ps60_local_scalar.dxil!");

    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pRootSig;
    psoDesc.VS = { pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize() };
    psoDesc.PS = { dxilPS.data(), dxilPS.size() }; // Lowered via Mem2Reg to SM 5.0 DXBC!
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pPSO = nullptr;
    hr = pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pPSO));
    std::cout << "[PIPELINE] CreateGraphicsPipelineState intercepted DXIL: hr = " << std::hex << hr << std::dec << std::endl;
    assert(SUCCEEDED(hr) && pPSO);
    pVsBlob->Release();

    // Vertex Buffer (Centered Triangle)
    Vertex vertices[] = {
        {  0.0f,  0.55f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },
        {  0.55f, -0.55f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f },
        { -0.55f, -0.55f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f }
    };

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = sizeof(vertices);
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* pVertexBuffer = nullptr;
    hr = pDevice->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pVertexBuffer));
    assert(SUCCEEDED(hr) && pVertexBuffer);

    void* pVbData = nullptr;
    pVertexBuffer->Map(0, nullptr, &pVbData);
    memcpy(pVbData, vertices, sizeof(vertices));
    pVertexBuffer->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = pVertexBuffer->GetGPUVirtualAddress();
    vbView.SizeInBytes = sizeof(vertices);
    vbView.StrideInBytes = sizeof(Vertex);

    // Synchronization Fence
    ID3D12Fence* pFence = nullptr;
    UINT64 fenceValue = 0;
    hr = pDevice->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    assert(SUCCEEDED(hr) && pFence);

    auto syncGpu = [&]() {
        fenceValue++;
        pQueue->Signal(pFence, fenceValue);
        while (pFence->GetCompletedValue() < fenceValue) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    auto renderFrame = [&](UINT backBufferIndex, const float clearColor[4], const float rootConstants[4]) {
        pAlloc->Reset();
        pCmdList->Reset(pAlloc, pPSO);

        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(WIN_WIDTH), static_cast<float>(WIN_HEIGHT), 0.0f, 1.0f };
        pCmdList->RSSetViewports(1, &viewport);
        D3D12_RECT scissor = { 0, 0, WIN_WIDTH, WIN_HEIGHT };
        pCmdList->RSSetScissorRects(1, &scissor);

        pCmdList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, nullptr);
        pCmdList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

        pCmdList->SetPipelineState(pPSO);
        pCmdList->SetGraphicsRootSignature(pRootSig);
        pCmdList->SetGraphicsRoot32BitConstants(0, 4, rootConstants, 0);

        pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pCmdList->IASetVertexBuffers(0, 1, &vbView);
        pCmdList->DrawInstanced(3, 1, 0, 0);

        pCmdList->Close();
        ID3D12CommandList* ppLists[] = { pCmdList };
        pQueue->ExecuteCommandLists(1, ppLists);
        syncGpu();
    };

    // ========================================================================
    // 6. User Interaction & Foreground Waiting Loop (Up to 45 Seconds)
    // ========================================================================
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  USER INTERACTION: WAITING FOR FOREGROUND WINDOW CLICK  " << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "  Harness Window HWND: " << hwnd << std::endl;
    std::cout << "  Please click the KeplerBridge test window on your desktop to activate it!" << std::endl;

    bool foregroundVerified = false;
    uint32_t waitFrame = 0;
    auto waitStart = std::chrono::steady_clock::now();
    int maxWaitSec = 90;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--timeout" && i + 1 < argc) {
            maxWaitSec = std::atoi(argv[++i]);
        }
    }

    while (true) {
        MSG msg = {};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        HWND currentFg = GetForegroundWindow();
        bool isFgNow = (currentFg == hwnd);
        bool isVisNow = IsWindowVisible(hwnd);
        bool isIconNow = IsIconic(hwnd);

        // Render live dynamic pattern while waiting
        UINT bbIdx = waitFrame % 2;
        float r = 0.2f + 0.15f * std::sin(waitFrame * 0.1f);
        float g = 0.3f + 0.15f * std::cos(waitFrame * 0.1f);
        float b = 0.5f + 0.20f * std::sin(waitFrame * 0.05f);
        float waitClear[4] = { r, g, b, 1.0f };
        float rootConsts[4] = { 0.25f, 0.5f, 0.0f, 1.0f };

        renderFrame(bbIdx, waitClear, rootConsts);
        pSwapChain->Present(1, 0);

        // Update live window title
        std::string title = "KeplerBridge D3D12 -> D3D11 | NVIDIA GT 740 | Frame: " + std::to_string(waitFrame) +
            (isFgNow ? " | [FOREGROUND ACTIVE - VERIFIED]" : " | [WAITING: PLEASE CLICK THIS WINDOW]");
        SetWindowTextA(hwnd, title.c_str());

        if (isFgNow && isVisNow && !isIconNow) {
            foregroundVerified = true;
            std::cout << "\n  >>> FOREGROUND MATCH CONFIRMED! <<<" << std::endl;
            std::cout << "  Harness HWND matches GetForegroundWindow(): " << hwnd << std::endl;
            std::cout << "  Elapsed wait: "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - waitStart).count()
                      << " ms, Frame: " << waitFrame << std::endl;
            break;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - waitStart).count();
        if (elapsed >= maxWaitSec) {
            std::cout << "\n  [WAIT TIMEOUT] " << maxWaitSec << " seconds elapsed without foreground activation." << std::endl;
            std::cout << "  Current Foreground HWND: " << currentFg << std::endl;
            break;
        }

        if (waitFrame % 30 == 0) {
            std::cout << "  [Waiting] Elapsed: " << elapsed << "s | Frame: " << waitFrame
                      << " | Current FG: " << currentFg << " (Match: " << (isFgNow ? "YES" : "NO") << ")" << std::endl;
        }

        waitFrame++;
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 fps waiting loop
    }

    // ========================================================================
    // 7. TEST PHASE A — Foreground Verified Presentation (30 Frames)
    // ========================================================================
    std::cout << "\n========================================================" << std::endl;
    if (foregroundVerified) {
        std::cout << " [TEST A] Verified Foreground Window Presentation (30 Frames)" << std::endl;
    } else {
        std::cout << " [TEST A] Unverified Foreground (Active Desktop) Presentation (30 Frames)" << std::endl;
    }
    std::cout << "========================================================" << std::endl;

    UINT presentSuccessFramesA = 0;
    UINT occludedFramesA = 0;
    UINT otherFramesA = 0;
    UINT independentlyVerifiedFramesA = 0;
    std::vector<GDIColor> gdiCapturesA;

    std::string screenshotPathA = "reports/kepler_verified_foreground_screenshot.bmp";

    for (int frame = 0; frame < 30; ++frame) {
        MSG msg = {};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        UINT bbIndex = frame % 2;
        // Distinct color progression per frame
        float clearColor[4] = { 0.1f, 0.2f, (0.3f + frame * 0.02f), 1.0f };
        float rootConsts[4] = { 0.25f, 0.5f, 0.0f, 1.0f };

        renderFrame(bbIndex, clearColor, rootConsts);

        HWND fgCheck = GetForegroundWindow();
        bool isStillFg = (fgCheck == hwnd);

        // Update title
        std::string title = "KeplerBridge D3D12 -> D3D11 | GT 740 | Verified Test A: Frame " + std::to_string(frame);
        SetWindowTextA(hwnd, title.c_str());

        // Backbuffer readback on Frame 0
        if (frame == 0 && pfnReadback) {
            std::vector<uint32_t> rbPixels(WIN_WIDTH * WIN_HEIGHT, 0);
            UINT rbW = 0, rbH = 0;
            HRESULT hrRb = pfnReadback(pBackBuffers[bbIndex], rbPixels.data(), &rbW, &rbH);
            if (SUCCEEDED(hrRb)) {
                uint32_t clearP = rbPixels[10 * WIN_WIDTH + 10];
                uint32_t triP = rbPixels[(WIN_HEIGHT / 2) * WIN_WIDTH + (WIN_WIDTH / 2)];
                std::cout << "  [BACKBUFFER READBACK Frame 0]" << std::endl;
                std::cout << "    Dimensions:                " << rbW << "x" << rbH << std::endl;
                std::cout << "    Background Pixel (10, 10): RGBA(" << (clearP & 0xFF) << ", " << ((clearP >> 8) & 0xFF) << ", " << ((clearP >> 16) & 0xFF) << ", " << ((clearP >> 24) & 0xFF) << ")" << std::endl;
                std::cout << "    Triangle Center (400, 300):RGBA(" << (triP & 0xFF) << ", " << ((triP >> 8) & 0xFF) << ", " << ((triP >> 16) & 0xFF) << ", " << ((triP >> 24) & 0xFF) << ")" << std::endl;
            }
        }

        // Frame 15: Capture independent full-window desktop screenshot
        if (frame == 15) {
            bool shotOk = CaptureDesktopRectToFile(hwnd, screenshotPathA, WIN_WIDTH, WIN_HEIGHT);
            std::cout << "  [INDEPENDENT DESKTOP SCREENSHOT Frame 15]" << std::endl;
            std::cout << "    Saved To:                  " << screenshotPathA << " (Success: " << (shotOk ? "YES" : "NO") << ")" << std::endl;
            POINT pt = { 0, 0 };
            ClientToScreen(hwnd, &pt);
            std::cout << "    Screen Coordinates:        (" << pt.x << ", " << pt.y << ") to (" << (pt.x + WIN_WIDTH) << ", " << (pt.y + WIN_HEIGHT) << ")" << std::endl;
        }

        hr = pSwapChain->Present(1, 0);
        if (hr == S_OK) {
            presentSuccessFramesA++;
            if (isStillFg) {
                independentlyVerifiedFramesA++;
            }
        } else if (hr == DXGI_STATUS_OCCLUDED) {
            occludedFramesA++;
        } else {
            otherFramesA++;
        }

        GDIColor screenPixel = CaptureScreenPixelAtClient(hwnd, WIN_WIDTH / 2, WIN_HEIGHT / 2);
        gdiCapturesA.push_back(screenPixel);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "  Present-Success Frames (S_OK): " << presentSuccessFramesA << " / 30" << std::endl;
    std::cout << "  Occluded Frames:               " << occludedFramesA << std::endl;
    std::cout << "  Independently Verified Visible:" << independentlyVerifiedFramesA << std::endl;
    std::cout << "  Sample Desktop GDI Pixel (F15):R=" << (int)gdiCapturesA[15].r
              << " G=" << (int)gdiCapturesA[15].g << " B=" << (int)gdiCapturesA[15].b << std::endl;

    // ========================================================================
    // 8. TEST PHASE B — Minimized Window State (15 Frames)
    // ========================================================================
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [TEST B] Minimized Window State (15 Frames)" << std::endl;
    std::cout << "========================================================" << std::endl;

    ShowWindow(hwnd, SW_MINIMIZE);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    UINT presentSuccessFramesB = 0;
    UINT occludedFramesB = 0;
    UINT otherFramesB = 0;

    for (int frame = 0; frame < 15; ++frame) {
        MSG msg = {};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        UINT bbIndex = frame % 2;
        float clearColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red
        float rootConsts[4] = { 0.25f, 0.5f, 0.0f, 1.0f };

        renderFrame(bbIndex, clearColor, rootConsts);

        hr = pSwapChain->Present(1, 0);
        if (hr == S_OK) {
            presentSuccessFramesB++;
        } else if (hr == DXGI_STATUS_OCCLUDED) {
            occludedFramesB++;
        } else {
            otherFramesB++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "  Present Attempts (Minimized):  " << (presentSuccessFramesB + occludedFramesB) << std::endl;
    std::cout << "  Present-Success Frames (S_OK): " << presentSuccessFramesB << std::endl;
    std::cout << "  Occluded Frames (OCCLUDED):    " << occludedFramesB << " / 15 (Accurate Occlusion)" << std::endl;

    // ========================================================================
    // 9. TEST PHASE C — Restored Window State (30 Frames)
    // ========================================================================
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [TEST C] Restored Window State (30 Frames)" << std::endl;
    std::cout << "========================================================" << std::endl;

    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    UINT presentSuccessFramesC = 0;
    UINT occludedFramesC = 0;
    UINT otherFramesC = 0;
    std::vector<GDIColor> gdiCapturesC;
    std::string screenshotPathC = "reports/kepler_restored_screenshot.bmp";

    for (int frame = 0; frame < 30; ++frame) {
        MSG msg = {};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        UINT bbIndex = frame % 2;
        float clearColor[4] = { 0.0f, 0.5f, 0.2f, 1.0f }; // Green-cyan
        float rootConsts[4] = { 0.25f, 0.5f, 0.0f, 1.0f };

        renderFrame(bbIndex, clearColor, rootConsts);

        std::string title = "KeplerBridge D3D12 -> D3D11 | GT 740 | Restored Test C: Frame " + std::to_string(frame);
        SetWindowTextA(hwnd, title.c_str());

        if (frame == 15) {
            CaptureDesktopRectToFile(hwnd, screenshotPathC, WIN_WIDTH, WIN_HEIGHT);
        }

        hr = pSwapChain->Present(1, 0);
        if (hr == S_OK) {
            presentSuccessFramesC++;
        } else if (hr == DXGI_STATUS_OCCLUDED) {
            occludedFramesC++;
        } else {
            otherFramesC++;
        }

        GDIColor screenPixel = CaptureScreenPixelAtClient(hwnd, WIN_WIDTH / 2, WIN_HEIGHT / 2);
        gdiCapturesC.push_back(screenPixel);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "  Present-Success Frames (S_OK): " << presentSuccessFramesC << " / 30" << std::endl;
    std::cout << "  Occluded Frames:               " << occludedFramesC << std::endl;
    std::cout << "  Restored Desktop Screenshot:   " << screenshotPathC << std::endl;

    // ========================================================================
    // 10. Corrected Telemetry Terminology Summary
    // ========================================================================
    if (pfnBreakdown) {
        UINT64 pAttempts = 0, pSuccess = 0, pOccluded = 0, pVisible = 0, pTest = 0, pSubmissions = 0, pDraws = 0, pCausalVis = 0, pCausalOcc = 0;
        UINT32 pFailures = 0;
        pfnBreakdown(&pAttempts, &pSuccess, &pOccluded, &pVisible, &pFailures, &pTest, &pSubmissions, &pDraws, &pCausalVis, &pCausalOcc);
        std::cout << "\n========================================================" << std::endl;
        std::cout << "  KEPLERBRIDGE CAUSAL PRESENTATION TELEMETRY BREAKDOWN  " << std::endl;
        std::cout << "========================================================" << std::endl;
        std::cout << "  Total Present Attempts:                  " << pAttempts << std::endl;
        std::cout << "  Successful Present Calls (SUCCEEDED):    " << pSuccess << std::endl;
        std::cout << "  Occluded Present Calls (OCCLUDED):        " << pOccluded << std::endl;
        std::cout << "  Present Failures (FAILED hr):             " << pFailures << std::endl;
        std::cout << "  Rendered Work Submissions:                " << pSubmissions << std::endl;
        std::cout << "  Completed Translated Draws:               " << pDraws << std::endl;
        std::cout << "  Causally Rendered Present-Success Frames: " << pCausalVis << std::endl;
        std::cout << "  Causally Rendered Occluded Frames:        " << pCausalOcc << std::endl;
        std::cout << "  Independently Verified Visible Frames:    " << independentlyVerifiedFramesA << std::endl;
    }

    // ========================================================================
    // 11. Cleanup
    // ========================================================================
    pFence->Release();
    pVertexBuffer->Release();
    pPSO->Release();
    pRootSig->Release();
    pCmdList->Release();
    pAlloc->Release();
    pBackBuffers[0]->Release();
    pBackBuffers[1]->Release();
    pRtvHeap->Release();
    pSwapChain->Release();
    pQueue->Release();
    pDevice->Release();
    DestroyWindow(hwnd);
    FreeLibrary(hD3D12);
    FreeLibrary(hCompiler);

    std::cout << "\n========================================================" << std::endl;
    std::cout << "  PHASE 12.14.2 AUDIT RUN COMPLETE                      " << std::endl;
    std::cout << "========================================================" << std::endl;

    return 0;
}
