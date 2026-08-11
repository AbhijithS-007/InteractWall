#include <windows.h>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) return 1;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, argv[1], nullptr, nullptr, FALSE, DEBUG_PROCESS, nullptr, nullptr, &si, &pi)) {
        std::cerr << "CreateProcess failed" << std::endl;
        return 1;
    }

    DEBUG_EVENT de;
    while (WaitForDebugEvent(&de, INFINITE)) {
        if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
            break;
        } else if (de.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT) {
            auto& out = de.u.DebugString;
            char* buf = new char[out.nDebugStringLength + 1];
            SIZE_T read = 0;
            HANDLE hProc = OpenProcess(PROCESS_VM_READ, FALSE, de.dwProcessId);
            if (hProc) {
                if (ReadProcessMemory(hProc, out.lpDebugStringData, buf, out.nDebugStringLength, &read)) {
                    buf[read] = 0;
                    std::cout << buf << std::flush;
                }
                CloseHandle(hProc);
            }
            delete[] buf;
        }
        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
    }
    return 0;
}
