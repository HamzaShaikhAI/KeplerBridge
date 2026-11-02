#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wtsapi32.h>
#include <iostream>
#include <string>
#include <vector>

std::string GetObjectName(HANDLE hObject) {
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

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 12.14.1 — Session & Desktop Audit   " << std::endl;
    std::cout << "========================================================" << std::endl;

    DWORD procId = GetCurrentProcessId();
    DWORD threadId = GetCurrentThreadId();
    DWORD procSessionId = 0xFFFFFFFF;
    ProcessIdToSessionId(procId, &procSessionId);
    DWORD activeConsoleSessionId = WTSGetActiveConsoleSessionId();

    std::cout << "Process ID:                  " << procId << std::endl;
    std::cout << "Current Thread ID:           " << threadId << std::endl;
    std::cout << "Process Session ID:          " << procSessionId << std::endl;
    std::cout << "Active Console Session ID:   " << activeConsoleSessionId << std::endl;
    std::cout << "Session ID Match:            " << (procSessionId == activeConsoleSessionId ? "YES" : "NO") << std::endl;

    // Window station
    HWINSTA hWinSta = GetProcessWindowStation();
    std::string winStaName = GetObjectName(hWinSta);
    std::cout << "Process Window Station:      " << winStaName << " (Handle: " << hWinSta << ")" << std::endl;

    // Thread desktop
    HDESK hThreadDesk = GetThreadDesktop(threadId);
    std::string threadDeskName = GetObjectName(hThreadDesk);
    std::cout << "Current Thread Desktop:      " << threadDeskName << " (Handle: " << hThreadDesk << ")" << std::endl;

    // OpenInputDesktop & Switch
    HDESK hInputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hInputDesk) hInputDesk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
    if (hInputDesk) {
        std::string inputDeskName = GetObjectName(hInputDesk);
        std::cout << "Input Desktop:               " << inputDeskName << " (Accessible)" << std::endl;
        BOOL switched = SetThreadDesktop(hInputDesk);
        std::cout << "Switched to Input Desktop:   " << (switched ? "YES" : "NO") << std::endl;
    } else {
        DWORD err = GetLastError();
        std::cout << "Input Desktop:               <UNAVAILABLE> (Error: " << err << ")" << std::endl;
    }

    // Top-level shell & foreground window on active desktop
    HWND hShell = GetShellWindow();
    HWND hForeground = GetForegroundWindow();
    HWND hDesktop = GetDesktopWindow();

    std::cout << "GetDesktopWindow():          " << hDesktop << std::endl;
    std::cout << "GetShellWindow():            " << hShell << std::endl;
    std::cout << "GetForegroundWindow():       " << hForeground << std::endl;

    if (hForeground) {
        char title[256] = {};
        GetWindowTextA(hForeground, title, sizeof(title));
        DWORD fgProcId = 0;
        GetWindowThreadProcessId(hForeground, &fgProcId);
        std::cout << "  Foreground Window Title:   \"" << title << "\"" << std::endl;
        std::cout << "  Foreground Process ID:     " << fgProcId << std::endl;
    } else {
        std::cout << "  Foreground Window:         NULL (No active foreground window on current desktop)" << std::endl;
    }

    return 0;
}
