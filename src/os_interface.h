#ifndef OS_INTERFACE_H
#define OS_INTERFACE_H

#include <stdint.h>  // For int32_t, uint64_t
#include <stdbool.h> // For bool, true, false
#include <stddef.h>  // For size_t

// Maximum length for a process name string
#define MAX_PROC_NAME 256

/**
 * ----------------------------------------------------------------------
 * THE CROSS-PLATFORM CONTRACT
 * ----------------------------------------------------------------------
 * Any file that includes this header can manage processes on
 * Windows OR Mac without changing a single line of code.
 * ----------------------------------------------------------------------
 */

/**
 * @brief Returns the Process ID (PID) of the window currently in the foreground.
 * * @return int32_t The PID of the active application. Returns -1 on error.
 */
int32_t os_get_active_pid(void);

/**
 * @brief Gets the name of the process from its PID.
 * * @param pid The Process ID to look up.
 * @param buffer A character array to store the name (e.g., "chrome.exe").
 * @param size The size of the buffer (use MAX_PROC_NAME).
 */
void os_get_process_name(int32_t pid, char* buffer, size_t size);

/**
 * @brief Freezes (pauses) the process execution.
 * * Windows Implementation: Iterates through threads and calls SuspendThread.
 * Mac Implementation: Sends SIGSTOP signal.
 * * @param pid The Process ID to freeze.
 * @return int 0 on success, non-zero on failure.
 */
int os_freeze_process(int32_t pid);

/**
 * @brief Thaws (resumes) the process execution.
 * * Windows Implementation: Iterates through threads and calls ResumeThread.
 * Mac Implementation: Sends SIGCONT signal.
 * * @param pid The Process ID to thaw.
 * @return int 0 on success, non-zero on failure.
 */
int os_thaw_process(int32_t pid);

/**
 * @brief Returns the current memory usage of the process.
 * * Windows Implementation: Uses GetProcessMemoryInfo (Working Set).
 * Mac Implementation: Uses mach_task_basic_info (Resident Size).
 * * @param pid The Process ID to check.
 * @return uint64_t Memory usage in Bytes.
 */
uint64_t os_get_memory_usage(int32_t pid);

#endif // OS_INTERFACE_H