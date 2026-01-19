#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include "os_interface.h"
#include <unistd.h>
#include <limits.h>

// --- STATS FILE ---
#define STATS_FILENAME "macnap.stats"

// --- CONFIGURATION DEFAULTS ---
#define MAX_TRACKED_APPS 7
#define CONFIG_FILENAME "macnap.conf"

// --- LOG FILE ---
#define LOG_FILENAME "macnap.log"

// --- PID FILE ---
#define PID_FILENAME "macnap.pid"

// Whitelist Settings
#define WHITELIST_FILENAME "whitelist.txt"
#define MAX_WHITELIST_ITEMS 20
char user_whitelist[MAX_WHITELIST_ITEMS][MAX_PROC_NAME]; // 2D array of strings
int user_whitelist_count = 0;

// Runtime Flags
bool flag_dry_run = false; // If true, we observe but do not freeze
bool flag_energy_mode = false; // If true, only freeze when on battery

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
    bool is_throttled;
} AppState;

AppState history[MAX_TRACKED_APPS];

// --- LIVE STATS SYSTEM ---
void update_stats_file(time_t start_time) {
    FILE *f = fopen(STATS_FILENAME, "w");
    if (f) {
        // Format: [START_TIME] [FROZEN_COUNT] [RAM_SAVED]
        fprintf(f, "%ld %d %llu", start_time, stats_frozen_count, stats_ram_saved_mb);
        fclose(f);
    }
}

// --- HELPER: INPUT CLEANING ---
void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// --- PID Manager ---
void create_pid_file() {
    FILE *f = fopen(PID_FILENAME, "w");
    if (f) {
        fprintf(f, "%d", getpid());
        fclose(f);
    }
}

void remove_pid_file() {
    remove(PID_FILENAME);
}

int get_running_pid() {
    FILE *f = fopen(PID_FILENAME, "r");
    if (!f) return 0; // File doesn't exist

    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1) pid = 0;
    fclose(f);
    return pid;
}

// Check if a process is actually alive in the OS
bool is_process_running(int pid) {
    if (pid <= 0) return false;
    // Signal 0 is a special null signal.
    // it doesn't kill the process, but returns 0 if the process exists.
    return (kill(pid, 0) == 0);
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

// --- CPU THROTTLE MANAGER ---
void manage_throttled_apps() {
    bool active_throttles = false;

    // Check if anyone needs throttling first to avoid useless sleeps
    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (history[i].valid && history[i].is_throttled) active_throttles = true;
    }

    if (!active_throttles) return;

    // PHASE 1: The Breath (100ms ON)
    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (history[i].valid && history[i].is_throttled) {
            os_thaw_process(history[i].pid);
        }
    }
    sleep_ms(100); 

    // PHASE 2: The Hold (We don't sleep here, the main loop handles the rest)
    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (history[i].valid && history[i].is_throttled) {
            os_freeze_process(history[i].pid);
        }
    }
}

void check_for_idlers() {
    // --- DAY 15: POWER CHECK ---
    if (flag_energy_mode) {
        if (os_is_on_ac_power()) {
            // We are plugged in, skip freezing
            return;
        }
    }

    time_t now = time(NULL);
    int32_t active_pid = os_get_active_pid();

    for (int i = 0; i < MAX_TRACKED_APPS; i++) {
        if (!history[i].valid) continue;
        if (history[i].pid == active_pid) continue; 
        
        // Skip completely frozen apps unless they are throttled (we still manage throttled ones)
        if (history[i].is_frozen && !history[i].is_throttled) continue; 

        // 1. Check Memory Usage & Elastic Thresholds
        uint64_t mem_bytes = os_get_memory_usage(history[i].pid);
        double mem_mb = (double)mem_bytes / (1024 * 1024);

        // Elastic threshold logic (Day 18)
        int pressure = os_get_memory_pressure();
        int dynamic_threshold = config_min_memory; // Default (e.g. 50MB)

        if (pressure < 50) {
            dynamic_threshold = 500;
        } else if (pressure < 80) {
            dynamic_threshold = 200;
        }

        // 2. The Gatekeeper
        if (mem_mb < dynamic_threshold) {
            // Uncomment below if you want to see debug logs for small apps
            // printf("[IGNORE] %s is too small (%.1f MB)\n", history[i].name, mem_mb);
            continue;
        }

        // --- THROTTLE DECISION ---
        bool has_assertion = os_has_power_assertion(history[i].pid);
        
        if (has_assertion) {
            // If it has an assertion (Download/Audio), we don't freeze fully.
            // Instead, we put it in THROTTLE mode.
            if (!history[i].is_throttled) {
                 printf(COLOR_YELLOW "[THROTTLE] %s is busy (Assertion Detected). Limiting CPU to 10%%." COLOR_RESET "\n", history[i].name);
                 history[i].is_throttled = true;
                 
                 // If it was already fully frozen, thaw it first so it can enter the cycle
                 if (history[i].is_frozen) {
                     os_thaw_process(history[i].pid);
                     history[i].is_frozen = false;
                     if (stats_frozen_count > 0) stats_frozen_count--; // Adjust stats
                 }
            }
            // Skip the standard freeze logic
            continue;
        } 
        else {
            // Assertion is gone (Download finished)
            if (history[i].is_throttled) {
                printf(COLOR_GREEN "[RELEASE] %s finished task. Resuming normal watch." COLOR_RESET "\n", history[i].name);
                history[i].is_throttled = false;
                os_thaw_process(history[i].pid); // Ensure it's fully awake
                history[i].is_frozen = false;
            }
        }
        // ---------------------------------------

        // If currently throttled, skip standard freezing checks
        if (history[i].is_throttled) continue;

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

    write_log("SYSTEM", "ENGINE STOPPED.");
    remove_pid_file();

    exit(0);
}

// HOT RELOAD
void handle_reload(int sig) {
    printf(COLOR_CYAN "\n[SIGNAL] Received SIGHUP. Reloading configuration..." COLOR_RESET "\n");
    write_log("SYSTEM", "Reloading Configuration (Hot Swap)");

    // Reload config
    load_config();
    // Reload whitelist
    load_whitelist();

    // we cant easily change the timeout logic mid-loop,
    // but the variables config_timeout and config_min_memory
    // update instantly for the NEXT check.
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
        if (pid > 0) exit(EXIT_SUCCESS);

        // 2. On success: The child process becomes the session leader
        if (setsid() < 0) exit(EXIT_FAILURE);

        // 3. Catch, Ignore and Handle Signals
        signal(SIGCHLD, SIG_IGN);
        signal(SIGHUP, SIG_IGN);

        // 4. Fork off for the second time (Safety best practice)
        pid = fork();
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS);

        // 5. Write the NEW child PID to the file so we can stop it later
        create_pid_file(); 

        // 6. Close all standard file descriptors
        // We cannot print to the terminal anymore!
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        // From now on, only write_log() and send_notification() will work.
    #endif
}

void print_status_report() {
    int pid = get_running_pid();
    bool running = (pid > 0 && is_process_running(pid));

    printf("\n" COLOR_BOLD "========================================\n");
    printf("   MacNap STATUS REPORT 📊\n");
    printf("========================================\n" COLOR_RESET);

    if (running) {
        printf("   State:       " COLOR_GREEN "● RUNNING (Daemon Active)" COLOR_RESET "\n");
        printf("   PID:         %d\n", pid);
        
        // Read the live stats
        FILE *f = fopen(STATS_FILENAME, "r");
        if (f) {
            time_t start_time = 0;
            int count = 0;
            uint64_t ram = 0;
            
            if (fscanf(f, "%ld %d %llu", &start_time, &count, &ram) == 3) {
                // Calculate Uptime
                time_t now = time(NULL);
                double uptime_sec = difftime(now, start_time);
                int hours = (int)uptime_sec / 3600;
                int minutes = ((int)uptime_sec % 3600) / 60;
                
                printf("   Uptime:      %dh %dm\n", hours, minutes);
                printf("   Apps Frozen: %d\n", count);
                printf("   RAM Saved:   " COLOR_CYAN "%llu MB" COLOR_RESET "\n", ram);
            }

            // Show pressure
            int pressure = os_get_memory_pressure();
            printf("   Sys Pressure:" COLOR_YELLOW " %d%%" COLOR_RESET "\n", pressure);
            fclose(f);
        } else {
            printf("   Stats:       " COLOR_YELLOW "Waiting for update..." COLOR_RESET "\n");
        }

        // Show Current Config
        printf("----------------------------------------\n");
        printf("   Timeout:     %ds\n", config_timeout);
        printf("   RAM Limit:   %d MB\n", config_min_memory);
    } 
    else {
        printf("   State:       " COLOR_RED "● STOPPED" COLOR_RESET "\n");
        printf("   To start:    ./MacNap --daemon\n");
    }
    printf("========================================\n\n");
}

void get_plist_path(char* buffer, size_t size) {
    const char* home = getenv("HOME");
    snprintf(buffer, size, "%s/Library/LaunchAgents/com.macnap.agent.plist", home);
}

void install_startup() {
    char exe_path[PATH_MAX];
    char plist_path[PATH_MAX];

    // 1. Get where MacNap is currently sitting
    if (getcwd(exe_path, sizeof(exe_path)) == NULL) return;

    // we need the full path to the binary
    // NOTE: Assumes you are running this from the folder containing the executable
    strcat(exe_path, "/MacNap");

    get_plist_path(plist_path, sizeof(plist_path));

    printf("   > Target: %s\n", plist_path);
    printf("   > Binary: %s\n", exe_path);

    // 2. write the LaunchAgent XML file
    FILE *f = fopen(plist_path, "w");
    if (!f) {
        printf(COLOR_RED "[ERROR] Could not write plist file!" COLOR_RESET "\n");
        return;
    }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
    fprintf(f, "<plist version=\"1.0\">\n");
    fprintf(f, "<dict>\n");
    fprintf(f, "    <key>Label</key>\n");
    fprintf(f, "    <string>com.macnap.agent</string>\n");
    fprintf(f, "    <key>ProgramArguments</key>\n");
    fprintf(f, "    <array>\n");
    fprintf(f, "        <string>%s</string>\n", exe_path);
    fprintf(f, "        <string>--daemon</string>\n"); // Always run as ghost
    fprintf(f, "    </array>\n");
    fprintf(f, "    <key>RunAtLoad</key>\n");
    fprintf(f, "    <true/>\n");
    fprintf(f, "    <key>StandardOutPath</key>\n");
    fprintf(f, "    <string>/tmp/macnap.out.log</string>\n");
    fprintf(f, "    <key>StandardErrorPath</key>\n");
    fprintf(f, "    <string>/tmp/macnap.err.log</string>\n");
    fprintf(f, "</dict>\n");
    fprintf(f, "</plist>\n");
    fclose(f);

    // 3. tell macOS to load it immediately
    char command[512];
    snprintf(command, sizeof(command), "launchctl load %s", plist_path);
    system(command);

    printf(COLOR_GREEN "[SUCCESS] MacNap will now start automatically at login!" COLOR_RESET "\n");
    printf(COLOR_YELLOW "(Do not move the MacNap file, or startup will fail!)" COLOR_RESET "\n");
}

void remove_startup() {
    char plist_path[PATH_MAX];
    get_plist_path(plist_path, sizeof(plist_path));

    // 1. unload from macOS
    char command[512];
    snprintf(command, sizeof(command), "launchctl unload %s", plist_path);
    system(command);

    // 2. delete the file
    remove(plist_path);

    printf(COLOR_CYAN "[INFO] Startup removed. MacNap is manual only." COLOR_RESET "\n");
}

// --- MAIN LOOP ---
int main(int argc, char* argv[]) {

    time_t session_start_time = time(NULL);
    // 1. PARSE ARGUMENTS
    bool force_setup = false;
    bool run_as_daemon = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("\nMacNap Usage:\n");
            printf("  ./MacNap            Run normally\n");
            printf("  ./MacNap --help     Show this message\n");
            printf("  ./MacNap --setup    Force configuration menu\n");
            printf("  ./MacNap --dry-run  Safe mode (No freezing)\n");
            printf("  ./MacNap --daemon   Run in background (no terminal output)\n");
            printf("  ./MacNap --reload   Reload configuration and whitelist\n");
            printf("  ./MacNap --status   Show current status report\n");
            printf("  ./MacNap --install-startup   Setup MacNap to start at login\n");
            printf("  ./MacNap --remove-startup    Remove MacNap from login items\n");
            printf("  ./MacNap --energy-mode  Enable Energy Mode (Freeze only on Battery)\n");
            printf("  ./MacNap --stop     Kill the background daemon.\n\n");
            return 0;
        }
        else if (strcmp(argv[i], "--install-startup") == 0) {
            install_startup();
            return 0;
        }
        else if (strcmp(argv[i], "--remove-startup") == 0) {
            remove_startup();
            return 0;
        }
        else if (strcmp(argv[i], "--status") == 0) {
            print_status_report();
            return 0;
        }
        else if (strcmp(argv[i], "--energy-mode") == 0) {
            flag_energy_mode = true;
            printf(COLOR_GREEN "[FLAG] Energy Mode: ENABLED (Freezing only on Battery)" COLOR_RESET "\n");
        }
        else if (strcmp(argv[i], "--reload") == 0) {
            int pid = get_running_pid();
            if (pid > 0 && is_process_running(pid)) {
                printf("Reloading MacNap Daemon (PID %d)...\n", pid);
                kill(pid, SIGHUP);
            } else {
                printf("MacNap is not running.\n");
            }
            return 0;
        }
        else if (strcmp(argv[i], "--stop") == 0) {
            int pid = get_running_pid();
            if (pid > 0 && is_process_running(pid)) {
                printf("Stopping MacNap Daemon (PID %d)...\n", pid);
                kill(pid, SIGINT);
                // wait a tiny bit for cleanup
                sleep_ms(500);
            } else {
                printf("MacNap is not running (or PID file missing.)\n");
            }
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

    // before starting, check if already running
    int existing_pid = get_running_pid();
    if (existing_pid > 0 && is_process_running(existing_pid)) {
        printf(COLOR_RED "[ERROR] MacNap is already running (PID %d)!" COLOR_RESET "\n", existing_pid);
        printf("Use './MacNap --stop' first.\n");
        return 1;
    }

    // If we are not a daemon, write the PID now.
    // If we are a daemon, it will be written after daemonizing.
    if (!run_as_daemon) {
        create_pid_file();
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

    // we do this after daemonize() so the ignore setting gets overwritten.
    signal(SIGINT, handle_exit);
    signal(SIGHUP, handle_reload);

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
        manage_throttled_apps();
        update_stats_file(session_start_time);
        sleep_ms(900); 
    }
    return 0;
}