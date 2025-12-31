# Memory & Process Optimizer

**A Cross-Platform Background Process Manager.**

This tool automatically detects which application you are currently using and "freezes" (pauses) other heavy applications running in the background. This saves RAM and Battery by stopping apps from using the CPU when you are not looking at them.

---

## Simple Explanation (What does it do?)
Imagine your computer is like a desk. If you have too many toys (apps) on the desk, you run out of space (Memory).

Usually, even if you aren't playing with a toy, it still takes up space and makes noise. This program watches you. When you stop playing with a toy for 10 seconds, this program magically "freezes" it so it stops moving and making noise. As soon as you look at it again, it instantly unfreezes so you can play.

**Result:** Your computer stays fast and cool, even with many apps open.

---

## Technical Details
This is a C11 Systems Engineering project that interacts directly with the Operating System Kernel.

### How it works
1.  **Monitoring:** The program constantly queries the Window Server to find the Process ID (PID) of your active window.
2.  **Tracking:** It maintains a history of recently used apps.
3.  **Freezing (The Core Logic):**
    * **macOS:** Uses `SIGSTOP` signals to remove the process from the CPU scheduler.
    * **Windows:** Uses the Toolhelp32 API to take a snapshot of threads and suspends them individually.
4.  **Thawing:** When you switch back to a frozen app, it detects the focus change and sends `SIGCONT` (Mac) or resumes threads (Windows) instantly.

### Current Status
* **macOS:** **Fully Functional.** Can detect windows via CoreGraphics, freeze/thaw via Signals, and includes a safety list to prevent crashing system apps (like Finder/Dock).
* **Windows:** **Experimental.** The logic for freezing threads is implemented but requires further testing for stability.

---

## 📂 Project Structure
The project is built using **CMake** to handle cross-platform compilation automatically.

```text
memory_management/
├── CMakeLists.txt          # Build script (Detects OS automatically)
├── src/
│   ├── main.c              # Main Logic: Timers, Whitelists, and Decisions
│   ├── os_interface.h      # The API Contract (Header file)
│   └── platform/
│       ├── mac_impl.c      # macOS Implementation (CoreGraphics, Signals)
│       └── win_impl.c      # Windows Implementation (Win32 API)
```

---

## How to Run

### Prerequisites

* C Compiler (GCC/Clang for Mac, MSVC/MinGW for Windows)
* CMake (Version 3.10 or higher)

### Build & Run on macOS

Open your terminal in the project folder and run:

```bash
mkdir build
cd build
cmake ..
make
./MacNap
```

### Build & Run on Windows

Open PowerShell in the project folder and run:

```powershell
mkdir build
cd build
cmake ..
cmake --build .
.\Debug\MacNap.exe
```

---

## Roadmap (Future Features)

* [ ] **RAM Thresholds:** Only freeze applications that are using more than 500MB of Memory.
* [ ] **User Configuration:** Allow users to set their own timeout (currently 10 seconds).
* [ ] **Windows Stability:** Improve the thread suspension logic for Windows apps.
* [ ] **Linux Support:** Add support for Linux using X11/Wayland detection.
