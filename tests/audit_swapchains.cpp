#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <iostream>
#include <string>
#include <vector>

const char* SwapEffectToString(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
    case DXGI_SWAP_EFFECT_DISCARD: return "DXGI_SWAP_EFFECT_DISCARD (0)";
    case DXGI_SWAP_EFFECT_SEQUENTIAL: return "DXGI_SWAP_EFFECT_SEQUENTIAL (1)";
    case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL (3)";
    case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "DXGI_SWAP_EFFECT_FLIP_DISCARD (4)";
    default: return "Unknown SwapEffect";
    }
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 12.14.1 — DXGI Swap Chain Audit    " << std::endl;
    std::cout << "========================================================" << std::endl;

    // First, test on current desktop
    HDESK hCurrentDesk = GetThreadDesktop(GetCurrentThreadId());
    DWORD needed = 0;
    GetUserObjectInformationA(hCurrentDesk, UOI_NAME, nullptr, 0, &needed);
    std::vector<char> deskName(needed + 1, 0);
    GetUserObjectInformationA(hCurrentDesk, UOI_NAME, deskName.data(), needed, &needed);
    std::cout << "Initial Thread Desktop: " << deskName.data() << std::endl;

    // Switch to input desktop "Default" if possible
    HDESK hInputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hInputDesk) {
        hInputDesk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
    }
    if (hInputDesk) {
        BOOL switched = SetThreadDesktop(hInputDesk);
        std::cout << "Switched to Input Desktop 'Default': " << (switched ? "YES" : "NO") << std::endl;
        HDESK hNew = GetThreadDesktop(GetCurrentThreadId());
        DWORD n2 = 0;
        GetUserObjectInformationA(hNew, UOI_NAME, nullptr, 0, &n2);
        std::vector<char> d2(n2 + 1, 0);
        GetUserObjectInformationA(hNew, UOI_NAME, d2.data(), n2, &n2);
        std::cout << "Active Thread Desktop: " << d2.data() << std::endl;
        std::cout << "GetForegroundWindow(): " << GetForegroundWindow() << std::endl;
        std::cout << "GetShellWindow():      " << GetShellWindow() << std::endl;
    } else {
        std::cout << "Failed to open input desktop: " << GetLastError() << std::endl;
    }

    // Window creation on active desktop
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "AuditSCClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        "Audit SwapChain Window",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, 640, 480,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Create D3D11 Device
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &pDevice, &fl, &pContext);
    std::cout << "D3D11CreateDevice: hr = " << std::hex << hr << std::dec << ", FeatureLevel: " << std::hex << fl << std::dec << std::endl;
    if (FAILED(hr)) return 1;

    IDXGIDevice* pDXGIDev = nullptr;
    pDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDev));
    IDXGIAdapter* pAdapter = nullptr;
    pDXGIDev->GetAdapter(&pAdapter);
    pDXGIDev->Release();

    IDXGIFactory2* pFactory2 = nullptr;
    pAdapter->GetParent(IID_PPV_ARGS(&pFactory2));
    pAdapter->Release();

    // ========================================================================
    // Audit C: Native D3D11 DISCARD
    // ========================================================================
    std::cout << "\n--- [Configuration C] Native D3D11 DISCARD ---" << std::endl;
    DXGI_SWAP_CHAIN_DESC1 reqDescC = {};
    reqDescC.Width = 640;
    reqDescC.Height = 480;
    reqDescC.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    reqDescC.Stereo = FALSE;
    reqDescC.SampleDesc.Count = 1;
    reqDescC.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    reqDescC.BufferCount = 2;
    reqDescC.Scaling = DXGI_SCALING_STRETCH;
    reqDescC.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    reqDescC.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    reqDescC.Flags = 0;

    std::cout << "Requested API:        IDXGIFactory2::CreateSwapChainForHwnd" << std::endl;
    std::cout << "Requested SwapEffect: " << SwapEffectToString(reqDescC.SwapEffect) << std::endl;
    std::cout << "Requested Buffers:    " << reqDescC.BufferCount << ", Format: " << reqDescC.Format << std::endl;

    IDXGISwapChain1* pSCC = nullptr;
    hr = pFactory2->CreateSwapChainForHwnd(pDevice, hwnd, &reqDescC, nullptr, nullptr, &pSCC);
    std::cout << "Creation Result:      hr = " << std::hex << hr << std::dec << std::endl;

    if (SUCCEEDED(hr) && pSCC) {
        DXGI_SWAP_CHAIN_DESC1 actDescC = {};
        pSCC->GetDesc1(&actDescC);
        std::cout << "Actual SwapEffect:    " << SwapEffectToString(actDescC.SwapEffect) << std::endl;
        std::cout << "Actual Buffers:       " << actDescC.BufferCount << ", Format: " << actDescC.Format
                  << ", Scaling: " << actDescC.Scaling << ", Flags: " << actDescC.Flags << std::endl;
        std::cout << "SwapChain COM Ptr:    " << (void*)pSCC << std::endl;

        // Render target setup
        ID3D11Texture2D* pBB = nullptr;
        pSCC->GetBuffer(0, IID_PPV_ARGS(&pBB));
        ID3D11RenderTargetView* pRTV = nullptr;
        pDevice->CreateRenderTargetView(pBB, nullptr, &pRTV);
        pBB->Release();

        int ok = 0, occ = 0, other = 0;
        for (int i = 0; i < 10; ++i) {
            float col[4] = { 0.2f, 0.4f, 0.6f, 1.0f };
            pContext->ClearRenderTargetView(pRTV, col);
            HRESULT hrP = pSCC->Present(1, 0);
            if (hrP == S_OK) ok++;
            else if (hrP == DXGI_STATUS_OCCLUDED) occ++;
            else other++;
        }
        std::cout << "Present(1, 0) (10 frames): S_OK=" << ok << ", OCCLUDED=" << occ << ", Other=" << other << std::endl;
        pRTV->Release();
        pSCC->Release();
    }

    // ========================================================================
    // Audit D: Native D3D11 FLIP_DISCARD
    // ========================================================================
    std::cout << "\n--- [Configuration D] Native D3D11 FLIP_DISCARD ---" << std::endl;
    DXGI_SWAP_CHAIN_DESC1 reqDescD = {};
    reqDescD.Width = 640;
    reqDescD.Height = 480;
    reqDescD.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    reqDescD.Stereo = FALSE;
    reqDescD.SampleDesc.Count = 1;
    reqDescD.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    reqDescD.BufferCount = 2;
    reqDescD.Scaling = DXGI_SCALING_STRETCH;
    reqDescD.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    reqDescD.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    reqDescD.Flags = 0;

    std::cout << "Requested API:        IDXGIFactory2::CreateSwapChainForHwnd" << std::endl;
    std::cout << "Requested SwapEffect: " << SwapEffectToString(reqDescD.SwapEffect) << std::endl;
    std::cout << "Requested Buffers:    " << reqDescD.BufferCount << ", Format: " << reqDescD.Format << std::endl;

    IDXGISwapChain1* pSCD = nullptr;
    hr = pFactory2->CreateSwapChainForHwnd(pDevice, hwnd, &reqDescD, nullptr, nullptr, &pSCD);
    std::cout << "Creation Result:      hr = " << std::hex << hr << std::dec << std::endl;

    if (SUCCEEDED(hr) && pSCD) {
        DXGI_SWAP_CHAIN_DESC1 actDescD = {};
        pSCD->GetDesc1(&actDescD);
        std::cout << "Actual SwapEffect:    " << SwapEffectToString(actDescD.SwapEffect) << std::endl;
        std::cout << "Actual Buffers:       " << actDescD.BufferCount << ", Format: " << actDescD.Format
                  << ", Scaling: " << actDescD.Scaling << ", Flags: " << actDescD.Flags << std::endl;
        std::cout << "SwapChain COM Ptr:    " << (void*)pSCD << std::endl;

        // Render target setup
        ID3D11Texture2D* pBB = nullptr;
        pSCD->GetBuffer(0, IID_PPV_ARGS(&pBB));
        ID3D11RenderTargetView* pRTV = nullptr;
        pDevice->CreateRenderTargetView(pBB, nullptr, &pRTV);
        pBB->Release();

        int ok = 0, occ = 0, other = 0;
        for (int i = 0; i < 10; ++i) {
            float col[4] = { 0.6f, 0.2f, 0.4f, 1.0f };
            pContext->ClearRenderTargetView(pRTV, col);
            HRESULT hrP = pSCD->Present(1, 0);
            if (hrP == S_OK) ok++;
            else if (hrP == DXGI_STATUS_OCCLUDED) occ++;
            else other++;
        }
        std::cout << "Present(1, 0) (10 frames): S_OK=" << ok << ", OCCLUDED=" << occ << ", Other=" << other << std::endl;
        pRTV->Release();
        pSCD->Release();
    }

    pFactory2->Release();
    pContext->Release();
    pDevice->Release();
    DestroyWindow(hwnd);
    return 0;
}
