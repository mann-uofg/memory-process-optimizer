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

// --- LOG FILE ---
#define LOG_FILENAME "macnap.log"

// Whitelist Settings
#define WHITELIST_FILENAME "whitelist.txt"
#define MAX_WHITELIST_ITEMS 20
char user_whitelist[MAX_WHITELIST_ITEMS][MAX_PROC_NAME]; // 2D array of strings
int user_whitelist_count = 0;

// Runtime Flags
bool flag_dry_run = false; // If true, we observe but do not freeze

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

// LOGGING SYSTEM
void write_log(const char* level, const char* message) {
    FILE *f = fopen(LOG_FILENAME, "a"); // 'a' = append mode
    if (f == NULL) return;

    // Current time logging
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    // write format: [TIME] [LEVEL] MESSAGE
    fprintf(f, "[%s] [%s] %s\n", time_str, level, message);
    fclose(f);
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

            // --- DAY 11: BLACK BOX LOGGING ---
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg), "Sentinel Emergency Thaw: %s", history[i].name);
            write_log("SENTINEL", log_msg);
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

                char log_msg[128];
                snprintf(log_msg, sizeof(log_msg), "Thawed %s (User Active)", history[i].name);
                write_log("THAW", log_msg);
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
            // printf("[IGNORE] %s is too small (%.1f MB)\n", history[i].name, mem_mb);
            continue;
        }

        double seconds_inactive = difftime(now, history[i].last_active_time);

        // 3. The Timeout
        if (seconds_inactive > config_timeout) {
            if (flag_dry_run) {
                printf(COLOR_YELLOW "[DRY-RUN] Would have frozen %s (PID %d). Saving %.0f MB." COLOR_RESET "\n", 
                       history[i].name, history[i].pid, mem_mb);
                
                // Reset timer so we don't spam the log every second
                history[i].last_active_time = time(NULL);
                continue; // Skip the actual freezing!
            }

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

                // --- DAY 11: BLACK BOX LOGGING ---
                write_log("FREEZE", msg);
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

// --- Daemonizer ---
void daemonize() {
    #ifdef _WIN32
        printf("Daemon mode not yet supported on Windows.\n");
        exit(1);
    #else
        // 1. Fork off the parent process
        pid_t pid = fork();

        // An error occurred
        if (pid < 0) exit(EXIT_FAILURE);

        // Success: Let the parent terminate
        // The terminal thinks the command is done.
        if (pid > 0) exit(EXIT_SUCCESS);

        // 2. On success: The child process becomes the session leader
        if (setsid() < 0) exit(EXIT_FAILURE);

        // 3. Catch, Ignore and Handle Signals
        signal(SIGCHLD, SIG_IGN);
        signal(SIGHUP, SIG_IGN);

        // 4. Fork off for the second time (safety best practice)
        pid = fork();
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS);

        // 5. Close all standard file decriptors
        // we cannot print to the terminal anymore
        // output sent to printf will disappear into the void.
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        // from now on, only write_log() and send_notification() will work.
    #endif
}

// --- MAIN LOOP ---
int main(int argc, char* argv[]) {
    signal(SIGINT, handle_exit);

    // 1. PARSE ARGUMENTS
    bool force_setup = false;
    bool run_as_daemon = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("\nMacNap Usage:\n");
            printf("  ./MacNap            Run normally\n");
            printf("  ./MacNap --setup    Force configuration menu\n");
            printf("  ./MacNap --dry-run  Safe mode (No freezing)\n");
            printf("  ./MacNap --help     Show this message\n\n");
            printf("  ./MacNap --daemon   Run in background (no terminal output)\n\n");
            return 0;
        }
        else if (strcmp(argv[i], "--daemon") == 0) {
            run_as_daemon = true;
        }
        else if (strcmp(argv[i], "--setup") == 0) force_setup = true;
        else if (strcmp(argv[i], "--dry-run") == 0) {
            flag_dry_run = true;
            printf(COLOR_YELLOW "[FLAG] Dry Run Mode: ENABLED" COLOR_RESET "\n");
        }
    }

    printf("\n" COLOR_BOLD "========================================\n");
    printf("   MacNap - AUTO CONFIGURATION\n");
    printf("========================================\n" COLOR_RESET);

    // 2. CONFIGURATION
    if (!force_setup && load_config()) {
        printf("   > Mode: AUTOMATIC (Loaded from 'macnap.conf')\n");
    } 
    else {
        if (force_setup) printf("   > Mode: FORCED SETUP\n");
        else             printf("   > Mode: FIRST RUN SETUP\n");

        printf("----------------------------------------\n");
        int input_val;

        printf("[1] Enter Freeze Timeout (Seconds) [Default: 10]: ");
        if (scanf("%d", &input_val) == 1 && input_val > 0) config_timeout = input_val;
        else clear_input_buffer(); 

        printf("[2] Enter Minimum RAM to Freeze (MB) [Default: 50]: ");
        if (scanf("%d", &input_val) == 1 && input_val > 0) config_min_memory = input_val;
        else clear_input_buffer();

        save_config();
    }

    load_whitelist();

    printf("\n" COLOR_BOLD "----------------------------------------\n");
    printf("   🚀 STARTING ENGINE...\n");
    printf("   > Target: " COLOR_RED "Apps idle > %d sec" COLOR_RESET "\n", config_timeout);
    printf("   > Filter: " COLOR_YELLOW "Apps > %d MB RAM" COLOR_RESET "\n", config_min_memory);
    if (flag_dry_run) printf("   > Mode:   " COLOR_YELLOW "DRY RUN (Simulation Only)" COLOR_RESET "\n");
    else              printf("   > System: " COLOR_GREEN "Sentinel & Notifications Active" COLOR_RESET "\n");
    printf("----------------------------------------\n" COLOR_RESET);
    printf(COLOR_CYAN "   (Press Ctrl+C to Stop Safely)" COLOR_RESET "\n\n");

    // 3. START THE LOOP
    // Tracking for the 'Permission Bug'
    int blind_counter = 0; 

    if (run_as_daemon) {
        printf("MacNap is going ghost! See 'macnap.log' for activity.\n\n");
        write_log("SYSTEM", "Daemon Mode Activated (Detached from Terminal)");
        daemonize();
        // After daemonizing, we cannot print to terminal anymore
    }

    while (1) {
        int32_t current_pid = os_get_active_pid();
        char current_name[MAX_PROC_NAME];

        if (current_pid > 0) {
            os_get_process_name(current_pid, current_name, MAX_PROC_NAME);

            // --- BUG FIX: PERMISSION DETECTOR ---
            // If the OS keeps telling us "WindowManager", it means we are BLIND.
            if (strcmp(current_name, "WindowManager") == 0) {
                blind_counter++;
                
                // If we see this 5 times in a row, ALERT THE USER.
                if (blind_counter > 4) {
                    printf(COLOR_RED "\n[CRITICAL ERROR] MACNAP IS BLIND!" COLOR_RESET "\n");
                    printf(COLOR_YELLOW "  macOS is hiding app names (returning 'WindowManager').\n");
                    printf("  This means Screen Recording permissions are broken.\n");
                    printf("  Run this command to fix it:\n" COLOR_RESET);
                    printf(COLOR_BOLD "  tccutil reset ScreenCapture com.apple.Terminal\n\n" COLOR_RESET);
                    
                    blind_counter = 0; // Reset so we don't spam too fast
                    sleep_ms(2000);    // Pause so user sees the message
                }
                
                // Still run sentinel just in case
                perform_speculative_thaw();
            }
            else if (strcmp(current_name, "loginwindow") == 0 || strcmp(current_name, "Dock") == 0) {
                // Normal Sentinel behavior for system UI
                perform_speculative_thaw();
                blind_counter = 0; // Reset counter, these are valid names
            }
            else {
                // Normal Operation: We see a real app!
                update_app_activity(current_pid);
                blind_counter = 0; // Reset counter, we are healthy
            }
        }

        check_for_idlers();
        sleep_ms(1000); 
    }
    return 0;
}