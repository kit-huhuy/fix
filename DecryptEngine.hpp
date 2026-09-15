/*
 * DecryptEngine.hpp — Final_Dispatch Hardware Breakpoint + Background Thread
 * ===========================================================================
 * Independent thread continuously polls hardware breakpoint, render thread
 * read-only caches, ioctl decoupled from rendering
 */

#ifndef DECRYPT_ENGINE_HPP
#define DECRYPT_ENGINE_HPP

#include "Kernel.hpp"
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace DecryptOffsets {
    // HOOK_LITERAL: Offset to Final_Dispatch wrapper in libUE4.so
    // IMPORTANT: This varies by UE4 version and binary build!
    // For com.proximabeta.mf.uamo:
    //   - UE4 4.24: typically 0x2400000-0x2600000
    //   - UE4 4.25: typically 0x2500000-0x2700000
    //   - UE4 4.26: typically 0x2600000-0x2800000
    //   - UE4 4.27: typically 0x2700000-0x2900000
    //
    // Use: strings libUE4.so | grep -i "ue4" or IDA/Ghidra to find Final_Dispatch
    constexpr uint64_t HOOK_LITERAL = 0x2500000;  // ← Update with actual offset from IDA
    
    // Fallback offsets to scan if primary fails (for version detection)
    static const uint64_t FALLBACK_OFFSETS[] = {
        0x2400000, 0x2450000, 0x2500000, 0x2550000, 0x2600000,
        0x2650000, 0x2700000, 0x2750000, 0x2800000, 0x2900000
    };
    static constexpr int FALLBACK_COUNT = 10;
    
    constexpr int64_t  WR_CONTEXT_DATA   = -8;
    constexpr uint64_t WR_VM_ENTRY_PTR   = 0xA0;
    constexpr uint64_t WR_FINAL_DISPATCH = 0x150;
    int IsDecode = 3;
}

struct ResolvedAddrs { 
    uint64_t wrapper_base; 
    uint64_t vm_base; 
    bool valid; 
    uint64_t used_offset;  // Track which offset worked
};

extern paradise_driver* g_driver;  // Forward declare from Kernel.cpp

class AddrResolver {
public:
    static ResolvedAddrs resolve(uint64_t libUE4_base) {
        ResolvedAddrs a = {};
        if (!libUE4_base || !g_driver) {
            printf("[Resolver] Error: Invalid libUE4_base (0x%llx) or g_driver (0x%llx)\n", 
                   (unsigned long long)libUE4_base, (unsigned long long)(intptr_t)g_driver);
            return a;
        }
        
        // Try primary offset first
        printf("[Resolver] Attempting primary offset 0x%llx...\n", 
               (unsigned long long)DecryptOffsets::HOOK_LITERAL);
        
        a = _try_offset(libUE4_base, DecryptOffsets::HOOK_LITERAL);
        if (a.valid) return a;
        
        printf("[Resolver] Primary offset failed, scanning fallback offsets...\n");
        
        // Scan fallback offsets
        for (int i = 0; i < DecryptOffsets::FALLBACK_COUNT; i++) {
            uint64_t offset = DecryptOffsets::FALLBACK_OFFSETS[i];
            printf("[Resolver] Trying fallback offset 0x%llx...\n", (unsigned long long)offset);
            
            a = _try_offset(libUE4_base, offset);
            if (a.valid) {
                printf("[Resolver] ✓ SUCCESS: Offset 0x%llx is valid!\n", (unsigned long long)offset);
                printf("[Resolver] → Consider updating HOOK_LITERAL = 0x%llx in DecryptEngine.hpp\n", 
                       (unsigned long long)offset);
                return a;
            }
        }
        
        printf("[Resolver] ✗ All offsets exhausted - address resolution failed\n");
        return a;
    }
    
private:
    static ResolvedAddrs _try_offset(uint64_t libUE4_base, uint64_t offset) {
        ResolvedAddrs a = {};
        
        if (!g_driver) return a;
        
        // Read potential wrapper base
        uint64_t candidate = g_driver->read<uint64_t>(libUE4_base + offset) & 0x00FFFFFFFFFFFFFFULL;
        
        printf("[Resolver]   @ 0x%llx: read 0x%llx\n", 
               (unsigned long long)(libUE4_base + offset), (unsigned long long)candidate);
        
        // Validation checks
        if (!candidate || candidate < 0x100000) {
            printf("[Resolver]   → Invalid candidate (too low)\n");
            return a;
        }
        
        // Sanity check: wrapper should be in heap/stack range (0x400000000000 - 0x800000000000 typically)
        if (candidate > 0xFFFFFFFFFFFFFFULL) {
            printf("[Resolver]   → Invalid candidate (address too high)\n");
            return a;
        }
        
        a.wrapper_base = candidate;
        
        // Try to read VM base
        a.vm_base = g_driver->read<uint64_t>(a.wrapper_base + DecryptOffsets::WR_VM_ENTRY_PTR) & 0x00FFFFFFFFFFFFFFULL;
        
        printf("[Resolver]   @ wrapper+0x%llx: read vm_base=0x%llx\n", 
               (unsigned long long)DecryptOffsets::WR_VM_ENTRY_PTR, (unsigned long long)a.vm_base);
        
        if (!a.vm_base || a.vm_base < 0x100000) {
            printf("[Resolver]   → Invalid VM base\n");
            return a;
        }
        
        a.valid = true;
        a.used_offset = offset;
        
        printf("[Decrypt] ✓ wrapper=0x%llx vm=0x%llx offset=0x%llx\n", 
               (unsigned long long)a.wrapper_base, 
               (unsigned long long)a.vm_base,
               (unsigned long long)offset);
        return a;
    }
};

class DecryptEngine {
public:
    static DecryptEngine& get() { 
        static DecryptEngine inst; 
        return inst; 
    }

    bool init(uint64_t libUE4_base) {
        if (_bp_id >= 0) return true;  // Already initialized
        
        if (!g_driver) {
            printf("[Decrypt] Error: paradise_driver not initialized\n");
            DecryptOffsets::IsDecode = 2;
            return false;
        }
        
        printf("[Decrypt] Resolving addresses from libUE4_base=0x%llx\n", 
               (unsigned long long)libUE4_base);
        
        _addr = AddrResolver::resolve(libUE4_base);
        if (!_addr.valid) {
            printf("[Decrypt] ✗ Error: Failed to resolve addresses\n");
            printf("[Decrypt] Troubleshooting:\n");
            printf("  1. Verify libUE4.so base address is correct\n");
            printf("  2. Use IDA/Ghidra to find Final_Dispatch offset in your binary\n");
            printf("  3. Check if target process (com.proximabeta.mf.uamo) is running\n");
            printf("  4. Verify Paradise driver has read access to target process\n");
            printf("  5. Try: objdump -s libUE4.so | grep -i dispatch\n");
            DecryptOffsets::IsDecode = 2;
            return false;
        }

        uint64_t bp_addr = _addr.wrapper_base + DecryptOffsets::WR_FINAL_DISPATCH;
        printf("[Decrypt] Setting breakpoint at: 0x%llx (Final_Dispatch)\n", 
               (unsigned long long)bp_addr);

        // Attach to process
        if (Kernel::hwbp_attach(Kernel::get_pid()) < 0) {
            printf("[Decrypt] ✗ Attach failed\n"); 
            DecryptOffsets::IsDecode = 2;
            return false;
        }

        // Set hardware breakpoint
        _bp_id = Kernel::hwbp_bp_set(bp_addr,
            Kernel::HWBP_ENABLED | Kernel::HWBP_CAPTURE | Kernel::HWBP_TIMING_BYPASS |
            Kernel::HWBP_INTERCEPT | Kernel::HWBP_DIAGNOSTIC | Kernel::HWBP_BAIT_GUARD,
            Kernel::HWBP_TYPE_X, 4);
        
        if (_bp_id < 0) { 
            printf("[Decrypt] ✗ Set breakpoint failed: %d\n", _bp_id); 
            DecryptOffsets::IsDecode = 1;
            return false;
        }

        // Enable breakpoint
        Kernel::hwbp_bp_enable(_bp_id);
        printf("[Decrypt] ✓ Breakpoint ID=%d ready\n", _bp_id);

        // Start background polling thread
        _running = true;
        _thread = std::thread(&DecryptEngine::_thread_loop, this);
        printf("[Decrypt] ✓ Background thread started\n");
        
        DecryptOffsets::IsDecode = 0;
        return true;
    }

    /*
     * Render thread call: Pure cache lookup, no ioctl
     */
    bool get(uint64_t rootComp, float& x, float& y, float& z) {
        std::lock_guard<std::mutex> lk(_mtx);
        auto it = _cache.find(rootComp);
        if (it == _cache.end()) return false;
        x = it->second.x; 
        y = it->second.y; 
        z = it->second.z;
        return true;
    }

    bool ready() const { return _bp_id >= 0; }

    void cleanup() {
        if (_running) {
            _running = false;
            if (_thread.joinable()) _thread.join();
            printf("[Decrypt] Background thread stopped\n");
        }
        if (_bp_id >= 0) { 
            Kernel::hwbp_bp_remove(_bp_id); 
            _bp_id = -1; 
        }
        Kernel::hwbp_reset();
        printf("[Decrypt] Breakpoints cleaned up\n");
    }

private:
    DecryptEngine() : _bp_id(-1), _last_hit(0), _running(false) {}

    /*
     * Background thread: Continuously poll breakpoint at 1ms interval
     */
    void _thread_loop() {
        while (_running) {
            _poll_internal();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /*
     * Internal poll: Check breakpoint → Write cache
     * (Cache write is locked, poll read is not)
     */
    void _poll_internal() {
        if (_bp_id < 0 || !g_driver) return;

        Kernel::hwbp_bp_info info;
        if (Kernel::hwbp_bp_get_info(_bp_id, info) < 0 || !info.has_snapshot) return;
        if (info.hit_count <= _last_hit) return;
        
        _last_hit = info.hit_count;
        
        printf("[Poll] hit=%lu clear=%lu dfi=%lu mdscr=%lu\n",
               info.hit_count, info.external_clear_count,
               info.dfi_restore_count, info.mdscr_restore_count);

        // Extract coordinates from register snapshot
        uint64_t comp = info.x[19];      // rootComponent in X19
        uint64_t transform = info.x[0];   // transform pointer in X0
        
        float x = g_driver->read<float>(transform + 0x10);
        float y = g_driver->read<float>(transform + 0x14);
        float z = g_driver->read<float>(transform + 0x18);

        // Validation: component pointer, coordinate range, no NaN
        if (comp > 0x100000
            && (x > 10.0f || x < -10.0f)
            && (y > 10.0f || y < -10.0f)
            && (z > 10.0f || z < -10.0f)
            && !std::isnan(x) && !std::isnan(y) && !std::isnan(z)) {
            
            printf("[Coord] X=%.2f Y=%.2f Z=%.2f (rootComp=0x%llx)\n", 
                   x, y, z, (unsigned long long)comp);
            
            // Thread-safe cache update
            std::lock_guard<std::mutex> lk(_mtx);
            _cache[comp] = {x, y, z};
        }
    }

    struct Vec3 { float x, y, z; };
    
    ResolvedAddrs _addr;
    int _bp_id;
    uint64_t _last_hit;
    std::map<uint64_t, Vec3> _cache;
    std::mutex _mtx;

    std::thread _thread;
    std::atomic<bool> _running;
};

#endif
