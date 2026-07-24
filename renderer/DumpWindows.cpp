#include <windows.h>
#include <iostream>

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    if (strcmp(className, "Progman") == 0 || strcmp(className, "WorkerW") == 0) {
        std::cout << "HWND: 0x" << std::hex << hwnd << std::dec << " | Class: " << className << "\n";
        
        // Print children
        HWND child = nullptr;
        while ((child = FindWindowExA(hwnd, child, nullptr, nullptr)) != nullptr) {
            char childClass[256];
            GetClassNameA(child, childClass, sizeof(childClass));
            std::cout << "  -> Child: 0x" << std::hex << child << std::dec << " | Class: " << childClass << "\n";
        }
    }
    return TRUE;
}

int main() {
    std::cout << "Dumping window tree...\n";
    EnumWindows(EnumWindowsProc, 0);
    return 0;
}
