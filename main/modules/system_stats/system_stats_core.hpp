/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace system_stats {

constexpr size_t kMaxTasks = 64;
constexpr size_t kTaskNameLen = 16;
constexpr size_t kMaxCores = 2;
// Busy threshold in hundredths of a percent of one core.
constexpr uint32_t kBusyWarnX100 = 9900;
// Allowed gap between the runtime-counter delta and the monotonic delta.
constexpr uint32_t kElapsedSkewPermille = 10;
// Longest window whose 32-bit runtime delta is still unambiguous.
constexpr int64_t kMaxWindowUs = 30LL * 60LL * 1000000LL;

enum class StackLoc : uint8_t { Unknown, Sram, Psram };
enum class TaskState : uint8_t { Unknown, Running, Ready, Blocked, Suspended, Deleted };
enum class WindowStatus : uint8_t {
    Ok,
    NoBaseline,
    SampleError,
    ZeroElapsed,
    ElapsedSkew,
    WindowTooLong,
};

struct RawTask {
    char name[kTaskNameLen];
    uintptr_t handle;
    uint32_t runtime;      // raw per-task runtime counter, microseconds
    uint32_t priority;
    uint32_t hwm_bytes;    // stack high-water mark, bytes
    int8_t core;           // -1 when the task has no affinity
    bool hwm_valid;
    TaskState state;
    StackLoc stack;
};

struct RawSample {
    bool valid;
    uint32_t total_runtime;   // raw total runtime counter, microseconds
    int64_t mono_us;          // monotonic clock at capture
    uint32_t live_tasks;      // task count reported by the scheduler
    uint16_t count;           // entries stored in tasks[]
    RawTask tasks[kMaxTasks];
};

struct TaskRow {
    char name[kTaskNameLen];
    uintptr_t handle;
    int8_t core;
    uint32_t priority;
    uint32_t hwm_bytes;
    bool hwm_valid;
    StackLoc stack;
    TaskState state;
    bool cpu_valid;
    bool is_new;
    uint32_t run_delta_us;
    uint32_t cpu_x100;        // percent of ONE core, hundredths
};

struct CoreBusy {
    bool valid;
    uint32_t idle_delta_us;
    uint32_t busy_x100;       // 100% - IDLE share, hundredths of a percent
};

struct CpuWindow {
    WindowStatus status;
    uint32_t elapsed_runtime_us;
    int64_t elapsed_mono_us;
    uint32_t cores;
    CoreBusy core[kMaxCores];
    uint16_t rows;
    uint16_t live_tasks;
    bool truncated;
    uint16_t created;
    uint16_t gone;
    uint16_t invalid_rows;
    TaskRow row[kMaxTasks];
};

struct HeapRegion {
    bool valid;
    size_t total;
    size_t free;
    size_t min_boot;
    size_t largest;
};

struct DmaRegion {
    bool valid;
    size_t free;
    size_t largest;
};

struct MemorySnapshot {
    HeapRegion sram;
    HeapRegion psram;
    DmaRegion dma;
};

struct MemSeen {
    bool sram_valid;
    bool psram_valid;
    size_t sram_min_free;
    size_t psram_min_free;
};

struct MemThresholds {
    size_t sram_largest_bytes;
    size_t sram_free_percent;
    size_t psram_largest_bytes;
    size_t psram_free_percent;
};

struct BuildInfo {
    const char *profile;
    const char *assertions;
    const char *idf;
    const char *app_version;
    const char *commit;
    const char *dirty;
    const char *elf;
};

struct ContextInfo {
    const char *screen;
    const char *screen_start;
    const char *power;
    const char *power_start;
    const char *wifi;
    const char *agent;
    const char *wifi_start;      // value at the window start; nullptr = not known
    const char *agent_start;
    uint32_t screen_changes;     // revision delta over the window (counts every transition, round trips too)
    uint32_t power_changes;
};

// Why a window cannot be compared with a steady-state baseline. Bit set = mixed.
constexpr uint32_t kMixScreen = 1U;
constexpr uint32_t kMixPower = 2U;
constexpr uint32_t kMixWifi = 4U;
constexpr uint32_t kMixAgent = 8U;

// Screen/power use exact revisions (round trips are caught); Wi-Fi/agent only compare the two ends,
// so a Wi-Fi/agent round trip inside one window is NOT detected.
uint32_t context_mix_mask(const ContextInfo &ctx);

struct SelfCost {
    bool valid;
    uint32_t sample_us;
    uint32_t mem_us;
    uint32_t emit_us;
};

struct Block {
    uint32_t seq;
    uint64_t uptime_ms;
    uint32_t period_ms;
    BuildInfo build;
    ContextInfo ctx;
    const char *store;
    uint32_t store_bytes;
    const CpuWindow *cpu;
    const MemorySnapshot *mem;
    const MemSeen *seen;
    const MemThresholds *thresholds;
    SelfCost prev_cost;
};

// level: 0 = info, 1 = warning.
using LineSink = void (*)(void *user, int level, const char *line);

// Computes per-task and per-core CPU use from two raw samples.
void compute_cpu_window(const RawSample &start, const RawSample &end, uint32_t cores, CpuWindow &out);

// Bit n set when core n is at or above kBusyWarnX100.
uint32_t busy_warning_mask(const CpuWindow &window);

void update_mem_seen(MemSeen &seen, const MemorySnapshot &mem);

// Writes the whole SYSTEM STATS block through the sink, one line per call.
void emit_block(const Block &block, LineSink sink, void *user);

const char *window_status_name(WindowStatus status);
const char *task_state_name(TaskState state);
const char *stack_loc_name(StackLoc loc);

} // namespace system_stats
