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
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

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
    // 1. Get the Process Group ID (PGID)
    pid_t pgid = getpgid(pid);
    
    if (pgid < 0) {
        // Fallback: If we can't find the group, just freeze the single PID
        return kill(pid, SIGSTOP);
    }

    // Safety: Never freeze our own group (MacNap)!
    // getpgid(0) returns the group of the calling process (us).
    if (pgid == getpgid(0)) { 
        printf("[SAFETY] Prevented freezing of MacNap's own group.\n");
        return -1; 
    }

    // 2. Freeze the WHOLE Group (Parent + Children)
    // killpg(pgid, signal) sends the signal to everyone in the family.
    return killpg(pgid, SIGSTOP);
}

int os_thaw_process(int32_t pid) {
    pid_t pgid = getpgid(pid);
    
    if (pgid < 0) {
        return kill(pid, SIGCONT);
    }
    
    // Thaw everyone in the family
    return killpg(pgid, SIGCONT);
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

// --- MEMORY PRESSURE ---
int os_get_memory_pressure() {
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm_stat;
    
    // Ask the kernel for VM stats
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info_t)&vm_stat, &count) != KERN_SUCCESS) {
        return 0; // Fail safe
    }

    // Calculate total pages
    uint64_t total_pages = vm_stat.active_count + vm_stat.inactive_count + vm_stat.free_count + vm_stat.wire_count;
    uint64_t active_pages = vm_stat.active_count + vm_stat.wire_count;

    if (total_pages == 0) return 0;

    // Return percentage used
    return (int)((active_pages * 100) / total_pages);
}

// --- ASSERTION CHECK ---
bool os_has_power_assertion(int32_t pid) {
    CFDictionaryRef assertions = NULL;
    
    // 1. Get all active power assertions from the OS
    if (IOPMCopyAssertionsByProcess(&assertions) != kIOReturnSuccess) {
        return false; // Could not check, assume safe to freeze
    }

    bool has_assertion = false;
    
    // 2. The dictionary keys are PIDs (as Numbers)
    long long pid_long = (long long)pid;
    CFNumberRef pid_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &pid_long);

    // 3. Check if our PID exists in the assertion list
    if (CFDictionaryContainsKey(assertions, pid_num)) {
        has_assertion = true;
    }

    // 4. Cleanup
    CFRelease(pid_num);
    CFRelease(assertions);

    return has_assertion;
}