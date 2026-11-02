#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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
    std::cout << "Testing Desktop Switch to Input Desktop..." << std::endl;

    HDESK hCurrent = GetThreadDesktop(GetCurrentThreadId());
    std::cout << "Current Thread Desktop: " << GetObjectName(hCurrent) << std::endl;

    // Try opening "Default" desktop directly
    HDESK hDefault = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
    if (!hDefault) {
        std::cout << "OpenDesktopA(\"Default\", GENERIC_ALL) failed: " << GetLastError() << std::endl;
        hDefault = OpenInputDesktop(0, FALSE, GENERIC_ALL);
        if (!hDefault) {
            std::cout << "OpenInputDesktop(GENERIC_ALL) failed: " << GetLastError() << std::endl;
            hDefault = OpenInputDesktop(0, FALSE, DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_SWITCHDESKTOP);
            if (!hDefault) {
                std::cout << "OpenInputDesktop(specific) failed: " << GetLastError() << std::endl;
            }
        }
    }

    if (hDefault) {
        std::cout << "Successfully opened target desktop: " << GetObjectName(hDefault) << std::endl;
        BOOL ok = SetThreadDesktop(hDefault);
        if (ok) {
            std::cout << "SetThreadDesktop SUCCEEDED!" << std::endl;
            HDESK hNow = GetThreadDesktop(GetCurrentThreadId());
            std::cout << "New Thread Desktop: " << GetObjectName(hNow) << std::endl;
            std::cout << "GetShellWindow():   " << GetShellWindow() << std::endl;
            std::cout << "GetForegroundWindow(): " << GetForegroundWindow() << std::endl;
        } else {
            DWORD err = GetLastError();
            std::cout << "SetThreadDesktop FAILED with error: " << err << std::endl;
        }
        CloseDesktop(hDefault);
    }

    return 0;
}
