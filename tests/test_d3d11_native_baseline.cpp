#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "  Native D3D11 Baseline Presentation Test                " << std::endl;
    std::cout << "========================================================" << std::endl;

    // Check environment
    std::cout << "GetForegroundWindow(): " << GetForegroundWindow() << std::endl;
    std::cout << "GetDesktopWindow():    " << GetDesktopWindow() << std::endl;
    std::cout << "GetShellWindow():      " << GetShellWindow() << std::endl;

    // Window
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "NativeD3D11TestClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_APPWINDOW,
        wc.lpszClassName,
        "Native D3D11 Test Window",
        WS_POPUP | WS_VISIBLE,
        100, 100, 640, 480,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    std::cout << "Created HWND: " << hwnd << std::endl;
    std::cout << "IsWindowVisible: " << IsWindowVisible(hwnd) << std::endl;
    std::cout << "IsIconic: " << IsIconic(hwnd) << std::endl;
    std::cout << "Current Foreground: " << GetForegroundWindow() << " (Match: " << (GetForegroundWindow() == hwnd) << ")" << std::endl;

    // D3D11 Device & SwapChain
    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 2;
    scDesc.BufferDesc.Width = 640;
    scDesc.BufferDesc.Height = 480;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    IDXGISwapChain* pSwapChain = nullptr;
    D3D_FEATURE_LEVEL fl;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &scDesc,
        &pSwapChain,
        &pDevice,
        &fl,
        &pContext);

    std::cout << "D3D11CreateDeviceAndSwapChain: hr = " << std::hex << hr << std::dec << ", FeatureLevel = " << std::hex << fl << std::dec << std::endl;
    if (FAILED(hr)) return 1;

    // Render Target
    ID3D11Texture2D* pBackBuffer = nullptr;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    ID3D11RenderTargetView* pRTV = nullptr;
    pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRTV);
    pBackBuffer->Release();

    int okCount = 0, occludedCount = 0, otherCount = 0;
    for (int i = 0; i < 20; ++i) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        float clearColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f }; // Bright green
        pContext->ClearRenderTargetView(pRTV, clearColor);

        HRESULT hrP = pSwapChain->Present(1, 0);
        if (hrP == S_OK) okCount++;
        else if (hrP == DXGI_STATUS_OCCLUDED) occludedCount++;
        else otherCount++;

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "Native D3D11 SwapChain Present(1, 0) Results (20 frames):" << std::endl;
    std::cout << "  S_OK:                 " << okCount << std::endl;
    std::cout << "  DXGI_STATUS_OCCLUDED: " << occludedCount << std::endl;
    std::cout << "  Other:                " << otherCount << std::endl;

    // Test FLIP_DISCARD
    std::cout << "\n--- Testing DXGI_SWAP_EFFECT_FLIP_DISCARD ---" << std::endl;
    DXGI_SWAP_CHAIN_DESC1 scDesc1 = {};
    scDesc1.BufferCount = 2;
    scDesc1.Width = 640;
    scDesc1.Height = 480;
    scDesc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc1.SampleDesc.Count = 1;
    scDesc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGIFactory2* pFactory2 = nullptr;
    CreateDXGIFactory1(IID_PPV_ARGS(&pFactory2));
    IDXGISwapChain1* pSwapChain1 = nullptr;
    if (pFactory2) {
        HRESULT hrSC = pFactory2->CreateSwapChainForHwnd(pDevice, hwnd, &scDesc1, nullptr, nullptr, &pSwapChain1);
        std::cout << "CreateSwapChainForHwnd (FLIP_DISCARD): hr = " << std::hex << hrSC << std::dec << std::endl;
        if (SUCCEEDED(hrSC) && pSwapChain1) {
            int okCountFlip = 0, occludedCountFlip = 0;
            for (int i = 0; i < 20; ++i) {
                HRESULT hrP = pSwapChain1->Present(1, 0);
                if (hrP == S_OK) okCountFlip++;
                else if (hrP == DXGI_STATUS_OCCLUDED) occludedCountFlip++;
            }
            std::cout << "Native D3D11 FLIP_DISCARD Present(1, 0) Results (20 frames):" << std::endl;
            std::cout << "  S_OK:                 " << okCountFlip << std::endl;
            std::cout << "  DXGI_STATUS_OCCLUDED: " << occludedCountFlip << std::endl;
            pSwapChain1->Release();
        }
        pFactory2->Release();
    }
    pRTV->Release();
    pSwapChain->Release();
    pContext->Release();
    pDevice->Release();
    DestroyWindow(hwnd);
    return 0;
}
