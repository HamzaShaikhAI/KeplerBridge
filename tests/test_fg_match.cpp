#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    HDESK hInputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hInputDesk) hInputDesk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
    if (hInputDesk) {
        SetThreadDesktop(hInputDesk);
    }

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "FgTestClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_APPWINDOW,
        wc.lpszClassName,
        "Foreground Test Window",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        200, 200, 400, 300,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    HWND fg = GetForegroundWindow();
    std::cout << "Created HWND:       " << hwnd << std::endl;
    std::cout << "Foreground HWND:    " << fg << std::endl;
    std::cout << "Foreground Match:   " << (hwnd == fg ? "YES (ACTIVE FOREGROUND)" : "NO") << std::endl;

    DestroyWindow(hwnd);
    return 0;
}
