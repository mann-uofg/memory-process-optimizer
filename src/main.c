#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h> // for string comparison
#include "os_interface.h"

// --- CONFIGURATION ---
#define MAX_TRACKED_APPS 5
#define FREEZE_TIMEOUT_SEC 10
#define MIN_MEMORY_MB 50

// --- CROSS-PLATFORM SLEEP ---
#ifdef _WIN32
    #include <windows.h>
    void sleep_ms(int ms) { Sleep(ms); }
#else
    #include <unistd.h>
    void sleep_ms(int ms) { usleep(ms * 1000); }
#endif

// --- DATA STRUCTURES ---
typedef struct {
    int32_t pid;
    char name[MAX_PROC_NAME];
    time_t last_active_time;
    bool is_frozen;
    bool valid; 
} AppState;

AppState history[MAX_TRACKED_APPS];

// --- CRITICAL SAFETY FILTER ---
// Returns true if the app is too important to freeze
bool is_critical_process(const char* name) {
    // List of apps that will crash your Mac if you freeze them
    const char* blacklist[] = {
        "Finder",
        "Dock",
        "WindowServer",
        "loginwindow",
        "kernel_task",
        "MacNap",       // Don't freeze yourself
        "Terminal",     // Don't freeze your own interface
        "iTerm2",
        "Code",         // Don't freeze your IDE while coding!
        NULL
    };

    for (int i = 0; blacklist[i] != NULL; i++) {
        // Check if the name contains the blacklisted word
        if (strstr(name, blacklist[i]) != NULL) {
            return true;
        }
    }
    return false;
}

// --- HELPER FUNCTIONS ---

void update_app_activity(int32_t pid) {
    // 1. Get the name first so we can check if it's safe
    char name[MAX_PROC_NAME];
    os_get_process_name(pid, name, MAX_PROC_NAME);

    // 2. SAFETY CHECK
    if (is_critical_process(name)) {
        // Silently ignore system processes so we don't crash the PC
        return; 
    }

    // 3. Check if we already know this app
    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (history[i].valid && history[i].pid == pid) {
            // FOUND IT!
            history[i].last_active_time = time(NULL); 
            
            // If it was frozen, THAW IT immediately!
            if (history[i].is_frozen) {
                printf("[ACTION] Welcome back, %s (PID %d). Thawing...\n", history[i].name, pid);
                os_thaw_process(pid);
                history[i].is_frozen = false;
            }
            return;
        }
    }

    // 4. If new, add it to the list
    static int next_slot = 0;
    
    printf("[INFO] Tracking new app: %s (PID %d)\n", name, pid);
    
    history[next_slot].pid = pid;
    strcpy(history[next_slot].name, name);
    history[next_slot].last_active_time = time(NULL);
    history[next_slot].is_frozen = false;
    history[next_slot].valid = true;

    next_slot = (next_slot + 1) % MAX_TRACKED_APPS;
}

void check_for_idlers() {
    time_t now = time(NULL);
    int32_t active_pid = os_get_active_pid();

    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (!history[i].valid) continue;
        if (history[i].is_frozen) continue; 
        if (history[i].pid == active_pid) continue; 

        // 1. Check Memory Usage (Skipping if below threshold)
        uint64_t mem_bytes = os_get_memory_usage(history[i].pid);
        double mem_mb = (double)mem_bytes / (1024 * 1024);

        // 2. The Gatekeeper: If it's too small, skip it.
        if (mem_mb < MIN_MEMORY_MB) {
            // Printing this once so we know why it's ignored
            printf("[IGNORE] %s is too small (%.1f MB)\n", history[i].name, mem_mb);
            continue;
        }

        double seconds_inactive = difftime(now, history[i].last_active_time);

        if (seconds_inactive > FREEZE_TIMEOUT_SEC) {
            printf("[Interface] %s (PID %d) inactive for %.0fs. Freezing!\n", 
                   history[i].name, history[i].pid, seconds_inactive);
            
            if (os_freeze_process(history[i].pid) == 0) {
                history[i].is_frozen = true;
            }
        }
    }
}

// --- MAIN LOOP ---
int main() {
    printf("========================================\n");
    printf("   MacNap - OS Interface MODE\n");
    printf("   (Safety Filters Active)\n");
    printf("========================================\n");

    while (1) {
        int32_t current_pid = os_get_active_pid();

        if (current_pid > 0) {
            update_app_activity(current_pid);
        }

        check_for_idlers();
        sleep_ms(1000); 
    }
    return 0;
}