#include "../os_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>             // For kill(), SIGSTOP, SIGCONT
#include <libproc.h>            // For process info (name, memory)
#include <ApplicationServices/ApplicationServices.h> // For Window detection
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

// --- 1. WINDOW DETECTION (CoreGraphics) ---
int32_t os_get_active_pid(void) {
    int32_t pid = -1;

    // Ask CoreGraphics for a list of all windows on screen, excluding the desktop itself
    CFArrayRef windowList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID
    );

    if (windowList == NULL) return -1;

    // The list is ordered from Front to Back. 
    // Therefore, the first valid window in the list is the Active Window.
    CFIndex count = CFArrayGetCount(windowList);
    for (CFIndex i = 0; i < count; i++) {
        CFDictionaryRef windowInfo = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);
        
        // Get the Window Layer. Normal apps are on Layer 0.
        // We skip system overlays (like volume HUDs or Spotlight) which are higher layers.
        CFNumberRef layerRef = CFDictionaryGetValue(windowInfo, kCGWindowLayer);
        int layer = 0;
        CFNumberGetValue(layerRef, kCFNumberIntType, &layer);

        if (layer == 0) {
            // Get the PID of the window owner
            CFNumberRef pidRef = CFDictionaryGetValue(windowInfo, kCGWindowOwnerPID);
            if (pidRef) {
                CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
                break; // Found the frontmost app!
            }
        }
    }

    // Clean up memory (CoreFoundation does not use Garbage Collection in C)
    CFRelease(windowList);
    return pid;
}

// --- 2. PROCESS NAME (libproc) ---
void os_get_process_name(int32_t pid, char* buffer, size_t size) {
    // proc_name copies the short name of the process into the buffer
    int result = proc_name(pid, buffer, (uint32_t)size);
    if (result <= 0) {
        snprintf(buffer, size, "Unknown");
    }
}

// --- 3. MEMORY USAGE (libproc) ---
uint64_t os_get_memory_usage(int32_t pid) {
    struct proc_taskinfo pti;
    
    // Query the kernel for task info
    // PROC_PIDTASKINFO is a specific flavor of info that includes memory stats
    int ret = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti));
    
    if (ret <= 0) {
        return 0; // Failed to get info (process might have died)
    }
    
    // pti_resident_size is the physical RAM usage (RSS)
    return pti.pti_resident_size;
}

// --- 4. FREEZE & THAW (Signals) ---
int os_freeze_process(int32_t pid) {
    // Send SIGSTOP: Tells the scheduler to remove this process from the run queue
    if (kill(pid, SIGSTOP) == 0) {
        return 0;
    }
    return -1;
}

int os_thaw_process(int32_t pid) {
    // Send SIGCONT: Tells the scheduler to resume the process
    if (kill(pid, SIGCONT) == 0) {
        return 0;
    }
    return -1;
}

bool os_is_on_ac_power() {
    // 1. Get a blob of power source info from the kernel
    CFTypeRef powerInfo = IOPSCopyPowerSourcesInfo();
    if (!powerInfo) return true; // Assume AC if error

    // 2. Get the list of power sources (Batteries, UPS, etc.)
    CFArrayRef powerSourcesList = IOPSCopyPowerSourcesList(powerInfo);
    if (!powerSourcesList) {
        CFRelease(powerInfo);
        return true;
    }

    bool is_ac = false;

    // 3. Loop through sources (Usually just one battery)
    for (CFIndex i = 0; i < CFArrayGetCount(powerSourcesList); i++) {
        CFTypeRef source = CFArrayGetValueAtIndex(powerSourcesList, i);
        CFDictionaryRef description = IOPSGetPowerSourceDescription(powerInfo, source);

        if (description) {
            // Check the "Power Source State" key
            CFStringRef state = CFDictionaryGetValue(description, CFSTR(kIOPSPowerSourceStateKey));
            
            // kIOPSACPowerValue means "Plugged In"
            if (CFStringCompare(state, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo) {
                is_ac = true;
                break;
            }
        }
    }

    // 4. Cleanup memory (Important in C!)
    CFRelease(powerSourcesList);
    CFRelease(powerInfo);

    return is_ac;
}