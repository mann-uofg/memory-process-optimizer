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

// Whitelist Settings
#define WHITELIST_FILENAME "whitelist.txt"
#define MAX_WHITELIST_ITEMS 20
char user_whitelist[MAX_WHITELIST_ITEMS][MAX_PROC_NAME]; // 2D array of strings
int user_whitelist_count = 0;

// --- ANSI COLORS ---
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"    // Freezing / Interface
#define COLOR_GREEN   "\033[32m"    // Thawing
#define COLOR_YELLOW  "\033[33m"    // Warnings
#define COLOR_CYAN    "\033[36m"    // Info / Stats
#define COLOR_BOLD    "\033[1m"     // Headers

// Runtime Configuration
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
void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// --- FILE I/O HELPERS ---
void save_config() {
    FILE *f = fopen(CONFIG_FILENAME, "w"); 
    if (f == NULL) {
        printf(COLOR_YELLOW "[WARN] Could not save configuration file." COLOR_RESET "\n");
        return;
    }
    fprintf(f, "%d %d", config_timeout, config_min_memory);
    fclose(f);
    printf(COLOR_CYAN "[DATA] Settings saved to '%s'" COLOR_RESET "\n", CONFIG_FILENAME);
}

bool load_config() {
    FILE *f = fopen(CONFIG_FILENAME, "r"); 
    if (f == NULL) return false;

    if (fscanf(f, "%d %d", &config_timeout, &config_min_memory) == 2) {
        fclose(f);
        return true; 
    }

    fclose(f);
    return false; 
}

// --- WHITELIST LOADER ---
void load_whitelist() {
    FILE *f = fopen(WHITELIST_FILENAME, "r");
    if (f == NULL) {
        // Create a deafult file so the user knows about it
        f = fopen(WHITELIST_FILENAME, "w");
        if (f) {
            fprintf(f, "Spotify\nDiscord\nActivity Monitor\n");
            fclose(f);
            printf(COLOR_CYAN "[DATA] Created deafult '%s'" COLOR_RESET "\n", WHITELIST_FILENAME);
        }
        return;
    }

    user_whitelist_count = 0;
    char line[MAX_PROC_NAME];

    while (fgets(line, sizeof(line), f)) {
        // clean up the line (remove newline and spaces)
        line[strcspn(line, "\r\n")] = 0; // Remove newline

        // skip empty lines or comments
        if (strlen(line) < 2 || line[0] == '#') continue;

        if (user_whitelist_count < MAX_WHITELIST_ITEMS) {
            strcpy(user_whitelist[user_whitelist_count], line);
            user_whitelist_count++;
        }
    }
    fclose(f);
    printf(COLOR_CYAN "[DATA] Loaded %d VIP apps from '%s'" COLOR_RESET "\n", user_whitelist_count, WHITELIST_FILENAME);
}

// --- CRITICAL SAFETY FILTER ---
// --- DEBUG SAFETY FILTER ---
bool is_critical_process(const char* name) {
    // 1. HARDCODED SYSTEM SAFETY LIST
    const char* blacklist[] = {
        "Finder", "Dock", "Electron", "WindowServer", "loginwindow",
        "kernel_task", "MacNap", "Terminal", "iTerm2", "Code", "clang", "make", NULL
    };

    for (int i = 0; blacklist[i] != NULL; i++) {
        if (strstr(name, blacklist[i]) != NULL) {
            // DEBUG PRINT: Tell us why it's blocked
            // printf(COLOR_YELLOW "[DEBUG] Ignoring '%s' (Matches Blacklist: '%s')\n" COLOR_RESET, name, blacklist[i]);
            return true;
        }
    }

    // 2. USER WHITELIST (VIPs)
    for (int i = 0; i < user_whitelist_count; i++) {
        // Paranoid check for empty strings
        if (strlen(user_whitelist[i]) < 1) continue;

        if (strstr(name, user_whitelist[i]) != NULL) {
            // !!! THIS IS THE IMPORTANT LINE !!!
            printf(COLOR_YELLOW "[DEBUG] Ignoring '%s' (Matches Whitelist: '%s')\n" COLOR_RESET, 
                   name, user_whitelist[i]);
            return true;
        }
    }

    return false;
}

// NOTIFICATIONS
void send_notification(const char* title, const char* message) {
    char command[512];
    // construct AppleScript command to show notification
    snprintf(command, sizeof(command),
             "osascript -e 'display notification \"%s\" with title \"%s\"'", message, title);
    system(command);
}

// BUG FIXING FUNCTION
void perform_speculative_thaw() {
    bool thawed_something = false;
    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (history[i].valid && history[i].is_frozen) {
            // Unfreeze everything so the user can enter
            os_thaw_process(history[i].pid);
            history[i].is_frozen = false;

            // Reset timer
            history[i].last_active_time = time(NULL);
            thawed_something = true;

            printf(COLOR_GREEN "[SENTINEL] UI Struggle Detected! Emergency Thaw: %s" COLOR_RESET "\n", 
                   history[i].name);
        }
    }

    // If we actually helped the user, tell them via notification
    if (thawed_something) {
        send_notification("MacNap Sentinel", "Unlock complete. Apps thawed for access.");
    }
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
                // GREEN for Thawing
                printf(COLOR_GREEN "[ACTION] Welcome back, %s (PID %d). Thawing..." COLOR_RESET "\n", history[i].name, pid);
                os_thaw_process(pid);
                history[i].is_frozen = false;
            }
            return;
        }
    }

    // Add new (Smart Eviction)
    static int next_slot = 0;

    if (history[next_slot].valid && history[next_slot].is_frozen) {
        // YELLOW for Warning
        printf(COLOR_YELLOW "[WARN] History full! Evicting frozen app %s (PID %d). Thawing first..." COLOR_RESET "\n", 
               history[next_slot].name, history[next_slot].pid);
        os_thaw_process(history[next_slot].pid);
        history[next_slot].is_frozen = false;
    }
    
    // CYAN for Info
    printf(COLOR_CYAN "[INFO] Tracking new app: %s (PID %d)" COLOR_RESET "\n", name, pid);
    
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

        // 2. The Gatekeeper
        if (mem_mb < config_min_memory) {
            // Uncomment below if you want to see debug logs for small apps
            printf("[IGNORE] %s is too small (%.1f MB)\n", history[i].name, mem_mb);
            continue;
        }

        double seconds_inactive = difftime(now, history[i].last_active_time);

        // 3. The Timeout
        if (seconds_inactive > config_timeout) {
            // RED for Freezing
            printf(COLOR_RED "[Interface] %s (PID %d) inactive for %.0fs. Freezing!" COLOR_RESET "\n", 
                   history[i].name, history[i].pid, seconds_inactive);
            
            if (os_freeze_process(history[i].pid) == 0) {
                history[i].is_frozen = true;

                // Update Statistics
                stats_frozen_count++;
                stats_ram_saved_mb += (uint64_t)mem_mb;
                
                // CYAN for Score
                printf(COLOR_CYAN "        (Score: %d freezes | +%.0f MB saved)" COLOR_RESET "\n", stats_frozen_count, mem_mb);

                // Send Notification
                char msg[128];
                snprintf(msg, sizeof(msg), "Froze %s (+%.0f MB RAM)", history[i].name, mem_mb);
                send_notification("MacNap Interface", msg);
            }
        }
    }
}

// --- SIGNAL HANDLER ---
void handle_exit(int sig) {
    printf("\n\n");
    printf(COLOR_BOLD "========================================\n");
    printf("   SESSION REPORT 📊\n");
    printf("========================================\n" COLOR_RESET);
    printf("   Apps Frozen:    %d\n", stats_frozen_count);
    printf("   RAM Reclaimed:  %llu MB\n", stats_ram_saved_mb);
    printf(COLOR_BOLD "========================================\n" COLOR_RESET);
    printf("   Cleaning up...\n\n");

    // Thaw every process we are tracking
    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (history[i].valid && history[i].is_frozen) {
            printf(COLOR_GREEN "[RESTORE] Emergency Thaw: %s (PID %d)" COLOR_RESET "\n", history[i].name, history[i].pid);
            os_thaw_process(history[i].pid);
            history[i].is_frozen = false;
        }
    }

    printf("[DONE] All Processes Restored. Exiting safely. Bye!\n\n");
    exit(0);
}

// --- MAIN LOOP ---
int main() {
    signal(SIGINT, handle_exit);

    printf("\n");
    printf(COLOR_BOLD "========================================\n");
    printf("   MacNap - AUTO CONFIGURATION\n");
    printf("========================================\n" COLOR_RESET);

    // 1. Try to Load Settings
    if (load_config()) {
        printf("   > Mode: AUTOMATIC (Loaded from 'macnap.conf')\n");
    } 
    else {
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

        // SAVE the settings
        save_config();
    }

    // Load Whitelist
    load_whitelist();

    // Summary
    printf("\n");
    printf(COLOR_BOLD "----------------------------------------\n");
    printf("   🚀 STARTING ENGINE...\n");
    // New Colorized Summary
    printf("   > Target: " COLOR_RED "Apps idle > %d sec" COLOR_RESET "\n", config_timeout);
    printf("   > Filter: " COLOR_YELLOW "Apps > %d MB RAM" COLOR_RESET "\n", config_min_memory);
    printf("   > System: " COLOR_GREEN "Sentinel & Notifications Active" COLOR_RESET "\n");
    printf("----------------------------------------\n" COLOR_RESET);
    printf(COLOR_CYAN "   (Press Ctrl+C to Stop Safely)" COLOR_RESET "\n\n");

    // 2. Start the Loop
    while (1) {
        int32_t current_pid = os_get_active_pid();
        char current_name[MAX_PROC_NAME];

        if (current_pid > 0) {
            os_get_process_name(current_pid, current_name, MAX_PROC_NAME);

            // --- SENTINEL CHECK (Bug Fix) ---
            // If the user clicks a frozen app, macOS often reports "WindowManager" or "loginwindow".
            if (strcmp(current_name, "WindowManager") == 0 || 
                strcmp(current_name, "loginwindow") == 0 ||
                strcmp(current_name, "Dock") == 0) {
                
                // User is stuck on the system layer -> THAW EVERYTHING
                perform_speculative_thaw();
            }
            else {
                // Normal operation
                update_app_activity(current_pid);
            }
        }

        check_for_idlers();
        sleep_ms(1000); 
    }
    return 0;
}