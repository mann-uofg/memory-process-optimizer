# MacNap: CPU/Memory Process Optimizer

> **A High-Performance Background Process Manager for macOS (Windows Port In-Progress).**

MacNap automatically detects which application you are currently using and "freezes" (pauses) other heavy applications running in the background. It effectively turns your computer into a **"Single-Tasking" machine**, maximizing CPU and RAM resources for the task at hand.

---

## 💡 The Concept
Imagine your computer is a desk. If you have too many toys (apps) on the desk, you run out of space (RAM).

Usually, even if you aren't playing with a toy, it still takes up space and makes noise. **MacNap watches you.** When you stop looking at an app for **10 seconds** (configurable), MacNap interacts with the kernel to "freeze" it. It stops moving, stops using CPU, and stops draining battery. As soon as you look at it again, it instantly unfreezes.

**Result:** Your computer stays fast, cool, and quiet, even with 50 apps open.

---

## 🚀 Key Features

*   **🧠 RAM Gatekeeper:** Only freezes apps consuming significant memory (User configurable, e.g., >50MB).
*   **🛡️ The Sentinel:** A safety mechanism that detects if the UI locks up. If you struggle to access a frozen app, the Sentinel instantly thaws everything.
*   **👻 Ghost Mode (Daemon):** Can run entirely in the background using `fork()` and `setsid()`. No terminal window required.
*   **🎟️ VIP Whitelist:** Supports a `whitelist.txt` file to prevent specific apps (like Spotify or Discord) from ever being frozen.
*   **⚡ Hot Swapping:** change your settings or whitelist while the program is running and reload instantly without restarting.
*   **📼 Flight Recorder:** Maintains a `macnap.log` file with timestamps of every freeze, thaw, and error.
*   **🔔 Native Notifications:** Sends macOS desktop notifications when taking action.

---

## 🛠️ Technical Architecture

This is a **C11 Systems Engineering** project that interacts directly with the Operating System Kernel.

1.  **Monitoring:** Queries the Window Server (CoreGraphics/Quartz) via [`src/platform/mac_impl.c`](src/platform/mac_impl.c) to find the Process ID (PID) of the active window.
2.  **Freezing:** Uses `SIGSTOP` signals to remove the process from the CPU scheduler.
3.  **Daemonization:** Uses Unix process forking in [`src/main.c`](src/main.c) to detach from the terminal and run as a system service.
4.  **IPC (Inter-Process Communication):** Uses `SIGHUP` signals to trigger configuration reloads and PID files to manage lifecycle.

**Current Status:**
*    **macOS:** **Production Ready.** Full feature set including Daemons, Notifications, and Safety Filters.
*    **Windows:** **In Development.** Core logic exists in [`src/platform/win_impl.c`](src/platform/win_impl.c), but the OS interface layer is currently being ported.

---

## 🎮 Installation & Usage

### 1. Build the Project
Open your terminal in the project folder:

```bash
mkdir build
cd build
cmake ..
make
```

### 2. Command Line Interface (CLI)
MacNap now supports advanced command-line flags.

| Command                        | Description                                                                 |
|--------------------------------|-----------------------------------------------------------------------------|
| `./MacNap`                     | Run in standard interactive mode                                           |
| `../MacNap --daemon`          | Ghost Mode. Runs in the background and closes terminal                    |
| `../MacNap --stop`            | Stops the background daemon safely                                         |
| `../MacNap --reload`          | Forces the daemon to re-read macnap.conf and whitelist.txt                |
| `../MacNap --dry-run`         | Simulation. Logs what would happen, but freezes nothing                   |
| `../MacNap --setup`           | Forces the First-Run Configuration menu to appear                          |

### ⚙️ Configuration
**macnap.conf** Stores your threshold settings. You can edit this manually or use --setup.
```
Format: [TIMEOUT_SECONDS] [MIN_MEMORY_MB]
Plaintext
10 50
```

**whitelist.txt** Add application names here (one per line) to protect them from freezing.
```
Plaintext
Spotify
Discord
Activity Monitor
Visual Studio Code
```
### 📂 Project Structure
```
memory_management/
├── CMakeLists.txt              # Cross-platform build script
├── macnap.conf                 # User Settings (Generated automatically)
├── whitelist.txt               # User VIP List (Generated automatically)
├── macnap.log                  # Activity Log
├── macnap.pid                  # Daemon Lock File
├── src/
│   ├── main.c                  # Core Logic: Daemon, Sentinel, Argument Parsing
│   ├── os_interface.h          # API Contract (Cross-platform header)
│   └── platform/
│       ├── mac_impl.c          # macOS Implementation (CoreGraphics, libproc)
│       └── win_impl.c          # Windows Implementation (Win32 API)
```
---

## 🗺️ Roadmap (Upcoming)

- [x] Memory Thresholds
- [x] Whitelisting
- [x] Background Daemon
- [x] Hot Reloading
- [ ] Windows Port: Finalize os_interface_win.c for full Windows 11 support.
- [ ] GUI Menu Bar App: Create a small icon in the top bar to control MacNap without the terminal.
- [ ] Energy Impact Mode: Only freeze apps when the laptop is on battery power.