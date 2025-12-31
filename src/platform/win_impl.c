#include "../os_interface.h"
#include <windows.h>
#include <psapi.h>      // For memory and name info
#include <tlhelp32.h>   // For snapshots (Freeze/Thaw logic)
#include <stdio.h>

// Helper to open a process with specific permissions
HANDLE get_process_handle(int32_t pid) {
    return OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
}

int32_t os_get_active_pid(void) {
    HWND hwnd = GetForegroundWindow();
    if (hwnd == NULL) return -1;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return (int32_t)pid;
}

void os_get_process_name(int32_t pid, char* buffer, size_t size) {
    // Set default in case of failure
    snprintf(buffer, size, "Unknown");

    HANDLE hProcess = get_process_handle(pid);
    if (hProcess) {
        HMODULE hMod;
        DWORD cbNeeded;
        // Get the first module (the executable itself)
        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            GetModuleBaseNameA(hProcess, hMod, buffer, size);
        }
        CloseHandle(hProcess);
    }
}

uint64_t os_get_memory_usage(int32_t pid) {
    HANDLE hProcess = get_process_handle(pid);
    if (!hProcess) return 0;

    PROCESS_MEMORY_COUNTERS pmc;
    uint64_t mem_usage = 0;

    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        // WorkingSetSize is the RAM currently in use (roughly)
        mem_usage = pmc.WorkingSetSize;
    }

    CloseHandle(hProcess);
    return mem_usage;
}

// --- THE HARD PART: FREEZE & THAW ---

// Helper function to iterate threads and toggle them
int toggle_process_threads(int32_t pid, bool freeze) {
    // 1. Take a snapshot of all threads in the system
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap == INVALID_HANDLE_VALUE) return -1;

    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);

    // 2. Get the first thread
    if (!Thread32First(hThreadSnap, &te32)) {
        CloseHandle(hThreadSnap);
        return -1;
    }

    // 3. Loop through every thread in the system
    do {
        // 4. Check if this thread belongs to our target Process ID
        if (te32.th32OwnerProcessID == pid) {
            
            // Open the thread with permission to suspend/resume
            HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
            
            if (hThread != NULL) {
                if (freeze) {
                    SuspendThread(hThread);
                } else {
                    ResumeThread(hThread);
                }
                CloseHandle(hThread);
            }
        }
    } while (Thread32Next(hThreadSnap, &te32));

    CloseHandle(hThreadSnap);
    return 0;
}

int os_freeze_process(int32_t pid) {
    // Don't freeze yourself or System processes!
    if (pid <= 4) return -1; 
    return toggle_process_threads(pid, true); // true = freeze
}

int os_thaw_process(int32_t pid) {
    return toggle_process_threads(pid, false); // false = thaw
}