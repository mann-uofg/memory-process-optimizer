#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include "os_interface.h"

// --- CONFIGURATION DEFAULTS ---
#define MAX_TRACKED_APPS 7
#define CONFIG_FILENAME "macnap.conf"

// These are now variables, not constants, so we can change them at runtime!
int config_timeout = 10;      // seconds
int config_min_memory = 50;   // MB

// Session Statistics
int stats_frozen_count = 0;
uint64_t stats_ram_saved_mb = 0;

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

// --- HELPER: INPUT CLEANING ---
// Clears the "Enter" key or garbage from the input buffer
void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// --- CRITICAL SAFETY FILTER ---
bool is_critical_process(const char* name) {
    const char* blacklist[] = {
        "Finder",
        "Dock",
        "Electron",
        "WindowServer",
        "loginwindow",
        "kernel_task",
        "MacNap",
        "Terminal",
        "iTerm2",
        "Code",
        NULL
    };

    for (int i = 0; blacklist[i] != NULL; i++) {
        if (strstr(name, blacklist[i]) != NULL) {
            return true;
        }
    }
    return false;
}

// --- CORE LOGIC ---

void update_app_activity(int32_t pid) {
    char name[MAX_PROC_NAME];
    os_get_process_name(pid, name, MAX_PROC_NAME);

    // SAFETY CHECK
    if (is_critical_process(name)) {
        return; 
    }

    // Check existing
    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (history[i].valid && history[i].pid == pid) {
            history[i].last_active_time = time(NULL); 
            
            if (history[i].is_frozen) {
                printf("[ACTION] Welcome back, %s (PID %d). Thawing...\n", history[i].name, pid);
                os_thaw_process(pid);
                history[i].is_frozen = false;
            }
            return;
        }
    }

    // Add new (Smart Eviction)
    static int next_slot = 0;

    if (history[next_slot].valid && history[next_slot].is_frozen) {
        printf("[WARN] History full! Evicting frozen app %s (PID %d). Thawing it first...\n", 
               history[next_slot].name, history[next_slot].pid);
        os_thaw_process(history[next_slot].pid);
        history[next_slot].is_frozen = false;
    }
    
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

        // 1. Check Memory Usage
        uint64_t mem_bytes = os_get_memory_usage(history[i].pid);
        double mem_mb = (double)mem_bytes / (1024 * 1024);

        // 2. The Gatekeeper (Using USER CONFIG variable now)
        if (mem_mb < config_min_memory) {
            // Uncomment below if you want to see debug logs for small apps
            // printf("[IGNORE] %s is too small (%.1f MB)\n", history[i].name, mem_mb);
            continue;
        }

        double seconds_inactive = difftime(now, history[i].last_active_time);

        // 3. The Timeout (Using USER CONFIG variable now)
        if (seconds_inactive > config_timeout) {
            printf("[Interface] %s (PID %d) inactive for %.0fs. Freezing!\n", 
                   history[i].name, history[i].pid, seconds_inactive);
            
            if (os_freeze_process(history[i].pid) == 0) {
                history[i].is_frozen = true;

                // Update Statistics
                stats_frozen_count++;
                stats_ram_saved_mb += (uint64_t)mem_mb;
                printf("        (Score: %d freezes | +%.0f MB saved)\n", stats_frozen_count, mem_mb);
            }
        }
    }
}

// --- SIGNAL HANDLER ---
void handle_exit(int sig) {
    printf("\n\n");
    printf("========================================\n");
    printf("   SESSION REPORT 📊\n");
    printf("========================================\n");
    printf("   Apps Frozen:    %d\n", stats_frozen_count);
    printf("   RAM Reclaimed:  %llu MB\n", stats_ram_saved_mb); // %llu for 64-bit int
    printf("========================================\n");
    printf("   Cleaning up...\n\n");

    // Thaw every process we are tracking
    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (history[i].valid && history[i].is_frozen) {
            printf("[RESTORE] Emergency Thaw: %s (PID %d)\n", history[i].name, history[i].pid);
            os_thaw_process(history[i].pid);
            history[i].is_frozen = false;
        }
    }

    printf("[DONE] All Processes Restored. Exiting safely. Bye!\n\n");
    exit(0);
}

void save_config() {
    FILE *f = fopen(CONFIG_FILENAME, "w"); // "w" = write mode
    if (f == NULL) {
        printf("[ERROR] Could not save configuration file.\n");
        return;
    }
    // we save 2 numbers separated by a space
    fprintf(f, "%d %d", config_timeout, config_min_memory);
    fclose(f);
    printf("[DATA] Settings saved to '%s'\n", CONFIG_FILENAME);
}

bool load_config() {
    FILE *f = fopen(CONFIG_FILENAME, "r"); // "r" = read mode
    if (f == NULL) {
        return false; // File does not exist yet
    }

    // read 2 integers
    if (fscanf(f, "%d %d", &config_timeout, &config_min_memory) == 2) {
        fclose(f);
        return true; // Successfully loaded
    }

    fclose(f);
    return false; // file existed but was empty or broken
}

// --- MAIN LOOP ---
int main() {
    signal(SIGINT, handle_exit);

    printf("\n");
    printf("========================================\n");
    printf("   MacNap - AUTO CONFIGURATION\n");
    printf("========================================\n");

    // 1. Try to Load Settings
    if (load_config()) {
        // SUCCESS: Loaded from file
        printf("   > Mode: AUTOMATIC (Loaded from 'macnap.conf')\n");
    } 
    else {
        // FAIL: First run setup
        printf("   > Mode: FIRST RUN SETUP\n");
        printf("----------------------------------------\n");
        
        int input_val;

        // Ask for Timeout
        printf("[1] Enter Freeze Timeout (Seconds) [Default: 10]: ");
        if (scanf("%d", &input_val) == 1 && input_val > 0) {
            config_timeout = input_val;
        } else {
            printf("    -> Using Default: 10s\n");
            clear_input_buffer(); 
        }

        // Ask for RAM Threshold
        printf("[2] Enter Minimum RAM to Freeze (MB) [Default: 50]: ");
        if (scanf("%d", &input_val) == 1 && input_val > 0) {
            config_min_memory = input_val;
        } else {
            printf("    -> Using Default: 50MB\n");
            clear_input_buffer();
        }

        // SAVE the settings so we don't ask next time
        save_config();
    }

    // Summary
    printf("\n");
    printf("----------------------------------------\n");
    printf("   🚀 STARTING ENGINE...\n");
    printf("   > Target: Apps idle for > %d seconds\n", config_timeout);
    printf("   > Filter: Apps using > %d MB RAM\n", config_min_memory);
    printf("----------------------------------------\n");
    printf("   (Press Ctrl+C to Stop Safely)\n\n");

    // 2. Start the Loop
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