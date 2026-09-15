/*
 * main.cpp — BuildDecrypted Entry Point
 * ===================================
 * Initialize Paradise driver, setup DecryptEngine, run coordinate capture
 */

#include "Kernel.hpp"
#include "DecryptEngine.hpp"
#include "paradise_api.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <atomic>

// Global state
static bool g_running = true;
static DecryptEngine* g_engine = nullptr;

static void signal_handler(int sig) {
    printf("\n[Main] Received signal %d", sig);
    if (sig == SIGSEGV || sig == SIGILL || sig == SIGABRT) {
        printf(" - likely due to missing Paradise driver / unsupported host");
    }
    printf(", shutting down...\n");
    g_running = false;
    std::quick_exit(EXIT_FAILURE);
}

static bool paradise_driver_available() {
    const bool has_android_env = (std::getenv("ANDROID_ROOT") != nullptr) ||
                                (std::getenv("ANDROID_DATA") != nullptr);
    const bool has_paradise_device = (access("/dev/paradise", F_OK) == 0) ||
                                    (access("/dev/paradise0", F_OK) == 0);
    
    printf("[Main] Android environment check:\n");
    printf("  ANDROID_ROOT: %s\n", has_android_env ? "YES" : "NO");
    printf("  /dev/paradise: %s\n", (access("/dev/paradise", F_OK) == 0) ? "YES" : "NO");
    printf("  /dev/paradise0: %s\n", (access("/dev/paradise0", F_OK) == 0) ? "YES" : "NO");
    
    return has_android_env || has_paradise_device;
}

void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGILL, signal_handler);
}

int find_target_process(const char* process_name) {
    if (!g_driver) {
        printf("[Main] Error: driver not initialized\n");
        return -1;
    }
    
    printf("[Main] Searching for process: %s\n", process_name);
    pid_t target_pid = g_driver->get_pid(process_name);
    if (target_pid <= 0) {
        printf("[Main] Could not find process: %s\n", process_name);
        printf("[Main] Tip: Run 'ps' on device to list processes\n");
        return -1;
    }
    
    printf("[Main] Found target process: %s (PID: %d)\n", process_name, target_pid);
    return target_pid;
}

uint64_t find_libue4_base(pid_t target_pid) {
    if (!g_driver) {
        printf("[Main] Error: driver not initialized\n");
        return 0;
    }
    
    printf("[Main] Initializing driver for PID %d...\n", target_pid);
    g_driver->initialize(target_pid);
    
    // Try to get libUE4 base address
    printf("[Main] Looking up libUE4.so base address...\n");
    uint64_t libue4_base = g_driver->get_module_base("libUE4.so");
    if (libue4_base == 0) {
        printf("[Main] Could not find libUE4.so base address\n");
        printf("[Main] Troubleshooting:\n");
        printf("  1. Verify target process is running\n");
        printf("  2. Check process has libUE4.so loaded: adb shell cat /proc/<pid>/maps | grep libUE4\n");
        printf("  3. Verify Paradise driver has read permissions\n");
        return 0;
    }
    
    printf("[Main] libUE4.so base: 0x%llx\n", (unsigned long long)libue4_base);
    return libue4_base;
}

int main(int argc, char* argv[]) {
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║        BuildDecrypted - Coordinate Extraction     ║\n");
    printf("║               Engine v1.0 (FIXED)                ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n\n");
    
    // Setup signal handlers for clean shutdown
    setup_signal_handlers();

    if (!paradise_driver_available()) {
        printf("[Main] ERROR: Paradise kernel driver is not available in this environment.\n");
        printf("[Main] This binary must run on Android with the Paradise driver loaded.\n");
        printf("[Main] Expected device: /dev/paradise or /dev/paradise0\n");
        printf("[Main] \n");
        printf("[Main] Setup instructions:\n");
        printf("  1. adb push driver_ko_601.ko /data/local/tmp/\n");
        printf("  2. adb shell insmod /data/local/tmp/driver_ko_601.ko\n");
        printf("  3. adb shell ls -la /dev/paradise* (verify device created)\n");
        printf("  4. Re-run this binary\n");
        return EXIT_FAILURE;
    }
    
    // Initialize Paradise driver
    printf("[Main] Initializing Paradise driver...\n");
    try {
        g_driver = new paradise_driver();
        printf("[Main] Paradise driver initialized successfully\n");
    } catch (const std::exception& e) {
        printf("[Main] ERROR: Failed to initialize Paradise driver: %s\n", e.what());
        printf("[Main] Check:\n");
        printf("  - Is /dev/paradise accessible?\n");
        printf("  - Do you have root permissions?\n");
        printf("  - Is the kernel module loaded? (adb shell lsmod | grep paradise)\n");
        return EXIT_FAILURE;
    }
    
    if (!g_driver) {
        printf("[Main] ERROR: Paradise driver is null\n");
        return EXIT_FAILURE;
    }
    
    // Get target process name (default: "com.proximabeta.mf.uamo")
    const char* target_process = "com.proximabeta.mf.uamo";
    if (argc > 1) {
        target_process = argv[1];
    }
    
    printf("[Main] Searching for process: %s\n", target_process);
    
    pid_t target_pid = find_target_process(target_process);
    if (target_pid <= 0) {
        printf("[Main] ERROR: Could not find target process\n");
        printf("[Main] Available processes:\n");
        printf("  - Run: adb shell ps | grep -i prox\n");
        printf("  - Or pass process name: %s <process_name>\n", argv[0]);
        delete g_driver;
        return EXIT_FAILURE;
    }
    
    // Find libUE4 base address
    uint64_t libue4_base = find_libue4_base(target_pid);
    if (libue4_base == 0) {
        printf("[Main] ERROR: Could not find libUE4 base address\n");
        printf("[Main] Verify target process:\n");
        printf("  adb shell ps | grep %s\n", target_process);
        printf("  adb shell cat /proc/%d/maps | grep libUE4\n", target_pid);
        delete g_driver;
        return EXIT_FAILURE;
    }
    
    // Initialize DecryptEngine
    printf("[Main] Initializing DecryptEngine...\n");
    g_engine = &DecryptEngine::get();
    
    if (!g_engine->init(libue4_base)) {
        printf("[Main] ERROR: Failed to initialize DecryptEngine\n");
        printf("[Main] \n");
        printf("[Main] SOLUTION - Find correct HOOK_LITERAL offset:\n");
        printf("  \n");
        printf("  Option A (Using IDA Pro):\n");
        printf("    1. Open libUE4.so in IDA\n");
        printf("    2. Search for: Final_Dispatch\n");
        printf("    3. Note the offset shown\n");
        printf("    4. Update DecryptEngine.hpp line 26: constexpr uint64_t HOOK_LITERAL = <offset>\n");
        printf("    5. Rebuild: ./build.sh\n");
        printf("  \n");
        printf("  Option B (Using strings):\n");
        printf("    1. adb pull /data/app/*/lib/arm64/libUE4.so .\n");
        printf("    2. strings libUE4.so | grep -i 'dispatch\\|final'\n");
        printf("    3. objdump -d libUE4.so | grep -i dispatch\n");
        printf("  \n");
        printf("  Option C (Automatic scan - already attempted):\n");
        printf("    Offsets 0x2400000-0x2900000 were tried. If all failed:\n");
        printf("    - Try wider range in AddrResolver::resolve()\n");
        printf("    - Check Paradise driver read permissions\n");
        printf("    - Verify target process still running\n");
        printf("\n");
        delete g_driver;
        return EXIT_FAILURE;
    }
    
    printf("[Main] ✓ DecryptEngine initialized successfully!\n");
    printf("[Main] Background thread polling for coordinates...\n\n");
    
    // Main loop - query coordinates periodically
    int capture_count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Optional: could add rendering or output logic here
        if (g_engine->ready()) {
            capture_count++;
            if (capture_count % 20 == 0) {  // Print status every 10 seconds
                printf("[Main] Engine running, collecting coordinates...\n");
            }
        }
    }
    
    // Cleanup
    printf("\n[Main] Shutting down...\n");
    if (g_engine) {
        g_engine->cleanup();
    }
    
    if (g_driver) {
        delete g_driver;
        g_driver = nullptr;
    }
    
    printf("[Main] Goodbye!\n");
    return EXIT_SUCCESS;
}
