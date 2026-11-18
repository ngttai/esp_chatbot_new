/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "profiler.hpp"
#include "private/utils.hpp"

#if CONFIG_EXAMPLE_SYSTEM_STATS
#include <cstdio>
#include <cstring>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif
#include "modules/ai_agents.hpp"
#include "modules/power_manager.hpp"
#include "modules/system_stats/system_stats_core.hpp"
#include "modules/system_stats_context.hpp"
#include "modules/wifi_control.hpp"
#endif

using namespace esp_brookesia;

constexpr const char *WORKER_NAME = "ProfilerWorker";
constexpr size_t WORKER_POLL_INTERVAL_MS = 100;

bool Profiler::init(const Config &config)
{
    if (is_initialized()) {
        BROOKESIA_LOGW("Profiler is already initialized");
        return true;
    }

    config_ = config;

    is_initialized_.store(true);

    BROOKESIA_LOGI("Profiler initialized");

    return true;
}

void Profiler::start_thread_profiler(bool enable_auto_logging)
{
    BROOKESIA_CHECK_FALSE_EXIT(is_initialized(), "Profiler is not initialized");
#if CONFIG_EXAMPLE_SYSTEM_STATS
    BROOKESIA_LOGW("Thread profiler tables are replaced by SYSTEM STATS");
    (void)enable_auto_logging;
    return;
#else

    auto &thread_profiler = lib_utils::ThreadProfiler::get_instance();
    thread_profiler.configure_profiling({
        .enable_auto_logging = enable_auto_logging,
    });
    BROOKESIA_CHECK_FALSE_EXIT(
        thread_profiler.start_profiling(config_.task_scheduler), "Failed to start thread profiler"
    );

    // Monitor idle tasks CPU usage
    auto profiling_slot = [this](const lib_utils::ThreadProfiler::ProfileSnapshot & snapshot) {
        std::vector<std::string> idle_task_names = {
#if CONFIG_SOC_CPU_CORES_NUM == 1
            "IDLE",
#elif CONFIG_SOC_CPU_CORES_NUM == 2
            "IDLE0",
            "IDLE1",
#endif
        };
        bool need_print_snapshot = false;
        for (auto &name : idle_task_names) {
            lib_utils::ThreadProfiler::TaskInfo task_info;
            BROOKESIA_CHECK_FALSE_EXIT(
                lib_utils::ThreadProfiler::get_task_by_name(snapshot, name, task_info),
                "Failed to get idle task `%1%`", name
            );
            if (task_info.cpu_percent < config_.thread_idle_cpu_usage_threshold) {
                need_print_snapshot = true;
                BROOKESIA_LOGW(
                    "The CPU usage of the idle task `%1%`(%2%%%) is lower than the threshold(%3%%%):", name,
                    task_info.cpu_percent, config_.thread_idle_cpu_usage_threshold
                );
            }
        }
        if (need_print_snapshot) {
            lib_utils::ThreadProfiler::print_snapshot(snapshot);
        }
    };
    auto profiling_connection = thread_profiler.connect_profiling_signal(profiling_slot);
    if (profiling_connection.connected()) {
        thread_profiler_connections_.push_back(std::move(profiling_connection));
    } else {
        BROOKESIA_LOGE("Failed to connect to thread profiler profiling signal");
    }

    // Monitor tasks with high stack usage
    auto threshold_slot = [](const std::vector<lib_utils::ThreadProfiler::TaskInfo> &tasks) {
        BROOKESIA_LOGW("The following tasks have high stack usage:");
        lib_utils::ThreadProfiler::print_snapshot({.tasks = tasks});
    };
    auto stack_usage_connection = thread_profiler.connect_threshold_signal(
                                      lib_utils::ThreadProfiler::ThresholdType::StackUsage,
                                      config_.thread_stack_usage_threshold, threshold_slot
                                  );
    if (stack_usage_connection.connected()) {
        thread_profiler_connections_.push_back(std::move(stack_usage_connection));
    } else {
        BROOKESIA_LOGE("Failed to connect to thread profiler stack usage threshold signal");
    }
#endif // CONFIG_EXAMPLE_SYSTEM_STATS
}

void Profiler::start_memory_profiler(bool enable_auto_logging)
{
    BROOKESIA_CHECK_FALSE_EXIT(is_initialized(), "Profiler is not initialized");

    auto &memory_profiler = lib_utils::MemoryProfiler::get_instance();
#if CONFIG_EXAMPLE_SYSTEM_STATS
    enable_auto_logging = false;
#endif
    memory_profiler.configure_profiling({
        .enable_auto_logging = enable_auto_logging,
    });
    BROOKESIA_CHECK_FALSE_EXIT(
        memory_profiler.start_profiling(config_.task_scheduler), "Failed to start memory profiler"
    );
#if !CONFIG_EXAMPLE_SYSTEM_STATS

    // Monitor largest free internal memory
    auto largest_free_internal_memory_threshold_slot = [this](const lib_utils::MemoryProfiler::ProfileSnapshot & snapshot) {
        BROOKESIA_LOGW(
            "Largest free internal memory(%1% B) reached the threshold(%2% B)",
            snapshot.memory.internal.largest_free_block, config_.mem_internal_largest_free_threshold
        );
    };
    auto largest_free_internal_memory_connection = memory_profiler.connect_threshold_signal(
                lib_utils::MemoryProfiler::ThresholdType::InternalLargestFreeBlock,
                config_.mem_internal_largest_free_threshold, largest_free_internal_memory_threshold_slot
            );
    if (largest_free_internal_memory_connection.connected()) {
        memory_profiler_connections_.push_back(std::move(largest_free_internal_memory_connection));
    } else {
        BROOKESIA_LOGE("Failed to connect to memory profiler largest free internal memory threshold signal");
    }

    // Monitor free internal memory
    auto free_internal_memory_threshold_slot = [this](const lib_utils::MemoryProfiler::ProfileSnapshot & snapshot) {
        BROOKESIA_LOGW(
            "Internal free memory(%1% B:~%2%%%) reached the threshold(%3% B:%4%%%)",
            snapshot.memory.internal.free_size, snapshot.memory.internal.free_percent,
            config_.mem_internal_free_percent_threshold * snapshot.memory.internal.total_size / 100,
            config_.mem_internal_free_percent_threshold
        );
    };
    auto free_internal_memory_connection = memory_profiler.connect_threshold_signal(
            lib_utils::MemoryProfiler::ThresholdType::InternalFreePercent,
            config_.mem_internal_free_percent_threshold, free_internal_memory_threshold_slot
                                           );
    if (free_internal_memory_connection.connected()) {
        memory_profiler_connections_.push_back(std::move(free_internal_memory_connection));
    } else {
        BROOKESIA_LOGE("Failed to connect to memory profiler free internal memory threshold signal");
    }

#if CONFIG_SPIRAM
    // Monitor largest free external memory
    auto largest_free_external_memory_threshold_slot = [this](const lib_utils::MemoryProfiler::ProfileSnapshot & snapshot) {
        BROOKESIA_LOGW(
            "Largest free external memory(%1% B) reached the threshold(%2% B)",
            snapshot.memory.external.largest_free_block, config_.mem_external_largest_free_threshold
        );
    };
    auto largest_free_external_memory_connection = memory_profiler.connect_threshold_signal(
                lib_utils::MemoryProfiler::ThresholdType::ExternalLargestFreeBlock,
                config_.mem_external_largest_free_threshold, largest_free_external_memory_threshold_slot
            );
    if (largest_free_external_memory_connection.connected()) {
        memory_profiler_connections_.push_back(std::move(largest_free_external_memory_connection));
    } else {
        BROOKESIA_LOGE("Failed to connect to memory profiler largest free external memory threshold signal");
    }

    // Monitor free external memory
    auto free_external_memory_threshold_slot = [this](const lib_utils::MemoryProfiler::ProfileSnapshot & snapshot) {
        BROOKESIA_LOGW(
            "External free memory(%1% B:~%2%%%) reached the threshold(%3% B:%4%%%)",
            snapshot.memory.external.free_size, snapshot.memory.external.free_percent,
            config_.mem_external_free_percent_threshold * snapshot.memory.external.total_size / 100,
            config_.mem_external_free_percent_threshold
        );
    };
    auto free_external_memory_connection = memory_profiler.connect_threshold_signal(
            lib_utils::MemoryProfiler::ThresholdType::ExternalFreePercent,
            config_.mem_external_free_percent_threshold, free_external_memory_threshold_slot
                                           );
    if (free_external_memory_connection.connected()) {
        memory_profiler_connections_.push_back(std::move(free_external_memory_connection));
    } else {
        BROOKESIA_LOGE("Failed to connect to memory profiler free external memory threshold signal");
    }
#endif // CONFIG_SPIRAM
#endif // !CONFIG_EXAMPLE_SYSTEM_STATS
}

#if CONFIG_EXAMPLE_SYSTEM_STATS

namespace {

constexpr const char *STATS_TAG = "SysStats";
constexpr uint32_t STATS_PERIOD_MS = 10000;
constexpr int64_t STATS_MIN_WINDOW_US = static_cast<int64_t>(STATS_PERIOD_MS) * 1000 / 2;

// uxTaskGetSystemState() fails (returns 0) when the array is smaller than the task count, so the scratch array
// is larger than the number of rows that are kept.
constexpr size_t STATS_SCRATCH_TASKS = system_stats::kMaxTasks + 16;

struct StatsStorage {
    system_stats::RawSample sample[2];
    system_stats::CpuWindow window;
    TaskStatus_t scratch[STATS_SCRATCH_TASKS];
};

struct StatsContext {
    const char *screen;
    const char *power;
    const char *wifi;
    const char *agent;
    uint32_t screen_rev;
    uint32_t power_rev;
};

struct StatsRuntime {
    bool started = false;
    StatsStorage *storage = nullptr;
    const char *store_where = nullptr;
    uint32_t store_bytes = 0;
    uint8_t base = 0;
    StatsContext base_ctx{};
    uint32_t seq = 0;
    system_stats::MemSeen seen{};
    system_stats::SelfCost cost{};
    system_stats::MemThresholds thresholds{};
    char elf[65] = {};
    bool output_failed = false;
    uint32_t failed_output_blocks = 0;
    esp_log_level_t output_level = ESP_LOG_INFO;
};

StatsRuntime g_stats;

system_stats::TaskState convert_state(eTaskState state)
{
    switch (state) {
    case eRunning: return system_stats::TaskState::Running;
    case eReady: return system_stats::TaskState::Ready;
    case eBlocked: return system_stats::TaskState::Blocked;
    case eSuspended: return system_stats::TaskState::Suspended;
    case eDeleted: return system_stats::TaskState::Deleted;
    default: return system_stats::TaskState::Unknown;
    }
}

// Stack placement from the stack base address recorded in the snapshot. The address is only compared against
// memory ranges; it is never dereferenced, and no task handle is used after the snapshot.
system_stats::StackLoc stack_location(const void *stack_base)
{
    if (stack_base == nullptr) {
        return system_stats::StackLoc::Unknown;
    }
    if (esp_ptr_internal(stack_base)) {
        return system_stats::StackLoc::Sram;
    }
    return esp_ptr_external_ram(stack_base) ? system_stats::StackLoc::Psram : system_stats::StackLoc::Unknown;
}

// Takes one raw scheduler sample into `out` with a single uxTaskGetSystemState() call.
//
// Task names are deliberately NOT read. TaskStatus_t::pcTaskName points into the task's TCB, and in the kernel ESP-IDF
// builds (components/freertos/FreeRTOS-Kernel/tasks.c) vTaskDelete() unlinks a task under the kernel lock but frees its TCB
// outside it, while vTaskSuspendAll() and uxTaskGetSystemState() hold no lock after they return. No public API copies a
// name under the kernel lock, so any later read of a borrowed name could be a use-after-free. Until a copy made at the
// source exists, a row is identified by what the kernel snapshot itself carries by value:
//   - label "T<n>" with n = TaskStatus_t::xTaskNumber (the TCB creation number) and the handle as an integer;
//   - the IDLE tasks are recognised by comparing the handle with xTaskGetIdleTaskHandleForCore() (a read of the
//     kernel's idle-handle array; idle tasks are never deleted), and labelled IDLE<core> ("IDLE" on one core).
// Handles are only compared as integers and never passed to a kernel API. Stack placement uses pxStackBase from the
// snapshot (address range test, never dereferenced).
void capture_sample(system_stats::RawSample &out, TaskStatus_t *snap)
{
    std::memset(&out, 0, sizeof(out));
    uint32_t total_runtime = 0;
    const UBaseType_t count = uxTaskGetSystemState(snap, STATS_SCRATCH_TASKS, &total_runtime);
    const int64_t mono_us = esp_timer_get_time();
    if (count == 0) {
        return;
    }
    TaskHandle_t idle[portNUM_PROCESSORS];
    for (BaseType_t core = 0; core < portNUM_PROCESSORS; ++core) {
        idle[core] = xTaskGetIdleTaskHandleForCore(core);
    }

    uint32_t live = 0;
    for (UBaseType_t i = 0; i < count; ++i) {
        const TaskStatus_t &ts = snap[i];
        if (ts.xHandle == nullptr) {
            continue;
        }
        ++live;
        if (out.count >= system_stats::kMaxTasks) {
            continue;
        }
        system_stats::RawTask &t = out.tasks[out.count++];
        int idle_core = -1;
        for (BaseType_t core = 0; core < portNUM_PROCESSORS; ++core) {
            if (ts.xHandle == idle[core]) {
                idle_core = static_cast<int>(core);
            }
        }
        if (idle_core < 0) {
            std::snprintf(t.name, sizeof(t.name), "T%lu", static_cast<unsigned long>(ts.xTaskNumber));
        } else if (portNUM_PROCESSORS == 1) {
            std::snprintf(t.name, sizeof(t.name), "IDLE");
        } else {
            std::snprintf(t.name, sizeof(t.name), "IDLE%d", idle_core);
        }
        t.handle = reinterpret_cast<uintptr_t>(ts.xHandle);
        t.runtime = ts.ulRunTimeCounter;
        t.priority = ts.uxCurrentPriority;
        // StackType_t is one byte in ESP-IDF, so the high-water mark is already in bytes.
        t.hwm_bytes = ts.usStackHighWaterMark;
        t.hwm_valid = true;
        t.core = (ts.xCoreID == tskNO_AFFINITY) ? -1 : static_cast<int8_t>(ts.xCoreID);
        t.state = convert_state(ts.eCurrentState);
        t.stack = stack_location(ts.pxStackBase);
    }
    out.mono_us = mono_us;
    out.total_runtime = total_runtime;
    out.live_tasks = live;
    out.valid = (live > 0);
}

void sample_memory(system_stats::MemorySnapshot &m)
{
    constexpr uint32_t sram_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    constexpr uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    constexpr uint32_t dma_caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    std::memset(&m, 0, sizeof(m));
    auto fill = [](system_stats::HeapRegion &r, uint32_t caps) {
        r.total = heap_caps_get_total_size(caps);
        if (r.total == 0) {
            return;
        }
        r.free = heap_caps_get_free_size(caps);
        r.min_boot = heap_caps_get_minimum_free_size(caps);
        r.largest = heap_caps_get_largest_free_block(caps);
        r.valid = true;
    };
    fill(m.sram, sram_caps);
    fill(m.psram, psram_caps);
    if (heap_caps_get_total_size(dma_caps) > 0) {
        m.dma.free = heap_caps_get_free_size(dma_caps);
        m.dma.largest = heap_caps_get_largest_free_block(dma_caps);
        m.dma.valid = true;
    }
}

const char *power_state_name(PowerPolicyState state)
{
    switch (state) {
    case PowerPolicyState::Boot: return "Boot";
    case PowerPolicyState::Active: return "Active";
    case PowerPolicyState::WaitAgentSleep: return "WaitAgentSleep";
    case PowerPolicyState::Dimmed: return "Dimmed";
    case PowerPolicyState::HardwareSleep: return "HardwareSleep";
    case PowerPolicyState::Waking: return "Waking";
    }
    return nullptr;
}

const char *wifi_state_name()
{
    if (!WifiControl::get_instance().is_enabled()) {
        return "Off";
    }
    if (!AI_Agents::get_instance().is_initialized()) {
        return "On,link=N/A";
    }
    return AI_Agents::get_instance().wifi_link_connected() ? "On,Connected" : "On,NotConnected";
}

StatsContext read_context()
{
    StatsContext c{};
    c.screen_rev = system_stats_ctx::screen_rev.load(std::memory_order_acquire);
    c.power_rev = system_stats_ctx::power_rev.load(std::memory_order_acquire);
    c.screen = system_stats_ctx::screen_name.load(std::memory_order_relaxed);
    c.power = power_state_name(PowerManager::get_instance().state());
    c.wifi = wifi_state_name();
    c.agent = AI_Agents::get_instance().state_name();
    return c;
}

void log_line(void *, int level, const char *line)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    const esp_log_level_t severity = level > 0 ? ESP_LOG_WARN : ESP_LOG_INFO;
    if (g_stats.output_failed || severity > g_stats.output_level) {
        return;
    }
    char packet[320];
    const int length = std::snprintf(packet, sizeof(packet), "%c (%lu) %s: %s\r\n",
                                     level > 0 ? 'W' : 'I', static_cast<unsigned long>(esp_log_timestamp()),
                                     STATS_TAG, line);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(packet)) {
        g_stats.output_failed = true;
    } else {
        // Serialize with normal stdio logging, but enqueue one whole line via the
        // public driver API. The VFS connection monitor can transiently return EIO
        // before it ever calls the driver, even when the queue is writable.
        flockfile(stdout);
        const int written = usb_serial_jtag_write_bytes(packet, static_cast<size_t>(length), pdMS_TO_TICKS(100));
        funlockfile(stdout);
        // The driver queues the whole item or nothing. On failure abandon this
        // block, rather than waiting once per remaining row or duplicating bytes.
        g_stats.output_failed = written != length;
    }
#else
    if (level > 0) {
        ESP_LOGW(STATS_TAG, "%s", line);
    } else {
        ESP_LOGI(STATS_TAG, "%s", line);
    }
#endif
}

const char *build_profile_name()
{
#if CONFIG_COMPILER_OPTIMIZATION_PERF
    return "PERF-O2";
#elif CONFIG_COMPILER_OPTIMIZATION_SIZE
    return "SIZE-Os";
#elif CONFIG_COMPILER_OPTIMIZATION_DEBUG
    return "DEBUG-Og";
#elif CONFIG_COMPILER_OPTIMIZATION_NONE
    return "NONE-O0";
#else
    return nullptr;
#endif
}

const char *build_assert_name()
{
#if CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE
    return "ENABLE";
#elif CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT
    return "SILENT";
#elif CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE
    return "DISABLE";
#else
    return nullptr;
#endif
}

system_stats::BuildInfo build_info()
{
    system_stats::BuildInfo info{};
    info.profile = build_profile_name();
    info.assertions = build_assert_name();
    info.idf = esp_get_idf_version();
    const esp_app_desc_t *desc = esp_app_get_description();
    info.app_version = (desc != nullptr) ? desc->version : nullptr;
#if defined(SYSTEM_STATS_GIT_REV)
    info.commit = SYSTEM_STATS_GIT_REV;
    info.dirty = SYSTEM_STATS_GIT_DIRTY;
#endif
    info.elf = g_stats.elf;
    return info;
}

bool stats_tick()
{
    StatsRuntime &r = g_stats;
    const int64_t t_begin = esp_timer_get_time();
    system_stats::RawSample &start = r.storage->sample[r.base];
    system_stats::RawSample &end = r.storage->sample[r.base ^ 1U];
    capture_sample(end, r.storage->scratch);
    const bool end_ok = end.valid;
    // Read right after the end sample. The baseline context was read right BEFORE its sample, so the revision window
    // is a superset of the CPU window: a transition near the edge is flagged, never missed.
    const StatsContext now = read_context();
    const int64_t t_sampled = esp_timer_get_time();
    if (start.valid && end.valid && ((end.mono_us - start.mono_us) < STATS_MIN_WINDOW_US)) {
        return true;
    }

    system_stats::CpuWindow &window = r.storage->window;
    system_stats::compute_cpu_window(start, end, portNUM_PROCESSORS, window);

    system_stats::MemorySnapshot mem;
    const int64_t t_mem_begin = esp_timer_get_time();
    sample_memory(mem);
    const int64_t t_mem_end = esp_timer_get_time();
    system_stats::update_mem_seen(r.seen, mem);

    system_stats::Block block{};
    block.seq = ++r.seq;
    block.uptime_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    block.period_ms = STATS_PERIOD_MS;
    block.build = build_info();
    block.ctx = {now.screen, r.base_ctx.screen, now.power, r.base_ctx.power, now.wifi, now.agent,
                 r.base_ctx.wifi, r.base_ctx.agent, now.screen_rev - r.base_ctx.screen_rev,
                 now.power_rev - r.base_ctx.power_rev};
    block.store = r.store_where;
    block.store_bytes = r.store_bytes;
    block.cpu = &window;
    block.mem = &mem;
    block.seen = &r.seen;
    block.thresholds = &r.thresholds;
    block.prev_cost = r.cost;

    const int64_t t_emit_begin = esp_timer_get_time();
    r.output_failed = false;
    r.output_level = esp_log_level_get(STATS_TAG);
    system_stats::emit_block(block, log_line, nullptr);
    if (r.output_failed) ++r.failed_output_blocks;
    char transport_status[96];
    std::snprintf(transport_status, sizeof(transport_status), "transport failed_blocks=%lu",
                  static_cast<unsigned long>(r.failed_output_blocks));
    log_line(nullptr, 0, transport_status);
    const int64_t t_emit_end = esp_timer_get_time();

    r.cost = {true, static_cast<uint32_t>(t_sampled - t_begin), static_cast<uint32_t>(t_mem_end - t_mem_begin),
              static_cast<uint32_t>(t_emit_end - t_emit_begin)};

    // The next window starts after the block was printed, so printing is not part of it.
    // The context is read before the sample so that the revision window contains the CPU window.
    // A failed end sample keeps the old baseline, so the next window is longer instead of silently shorter.
    if (end_ok) {
        const StatsContext next_ctx = read_context();
        capture_sample(end, r.storage->scratch);
        if (end.valid) {
            r.base ^= 1U;
            r.base_ctx = next_ctx;
        }
    }
    return true;
}

} // namespace

bool Profiler::start_system_stats()
{
    BROOKESIA_CHECK_FALSE_RETURN(is_initialized(), false, "Profiler is not initialized");
    BROOKESIA_CHECK_NULL_RETURN(config_.task_scheduler, false, "Scheduler is not valid");
    if (g_stats.started) {
        return true;
    }

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    // The polling VFS silently drops bytes after its 50 ms TX timeout.
    // Buffer the diagnostic burst through the public interrupt-driven driver.
    // Only the opt-in stats build changes console transport.
    bool installed_here = false;
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t usb_config = {
            .tx_buffer_size = 8192,
            .rx_buffer_size = 256,
        };
        const esp_err_t err = usb_serial_jtag_driver_install(&usb_config);
        BROOKESIA_CHECK_FALSE_RETURN(err == ESP_OK, false, "Failed to initialize stats USB transport: %1%", err);
        installed_here = true;
    }
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI(STATS_TAG, "transport=USJ_DRIVER tx_buffer_B=%u rx_buffer_B=%u existing_driver=%d",
             installed_here ? 8192U : 0U, installed_here ? 256U : 0U, !installed_here);
#endif

    constexpr size_t storage_bytes = sizeof(StatsStorage);
    void *memory = heap_caps_calloc(1, storage_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_stats.store_where = "PSRAM";
    if (memory == nullptr) {
        memory = heap_caps_calloc(1, storage_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        g_stats.store_where = "SRAM";
    }
    BROOKESIA_CHECK_NULL_RETURN(memory, false, "Failed to allocate %1% B for SYSTEM STATS", storage_bytes);
    g_stats.storage = static_cast<StatsStorage *>(memory);
    g_stats.store_bytes = static_cast<uint32_t>(storage_bytes);
    g_stats.thresholds = {
        config_.mem_internal_largest_free_threshold, config_.mem_internal_free_percent_threshold,
        config_.mem_external_largest_free_threshold, config_.mem_external_free_percent_threshold,
    };
    char elf_full[65] = {};
    // The helper may return only CONFIG_APP_RETRIEVE_LEN_ELF_SHA characters.
    // The application descriptor carries all 32 bytes regardless of that limit.
    const esp_app_desc_t *app = esp_app_get_description();
    if (app != nullptr) {
        constexpr char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < sizeof(app->app_elf_sha256); ++i) {
            const uint8_t byte = app->app_elf_sha256[i];
            elf_full[2 * i] = hex[byte >> 4];
            elf_full[2 * i + 1] = hex[byte & 0x0f];
        }
    }
    std::memcpy(g_stats.elf, elf_full, sizeof(g_stats.elf) - 1);

    g_stats.base_ctx = read_context();
    capture_sample(g_stats.storage->sample[0], g_stats.storage->scratch);
    g_stats.base = 0;

    ESP_LOGI(STATS_TAG, "SYSTEM STATS enabled period_ms=%lu max_tasks=%lu store=%s store_bytes=%lu elf_sha256=%s",
             static_cast<unsigned long>(STATS_PERIOD_MS), static_cast<unsigned long>(system_stats::kMaxTasks),
             g_stats.store_where, static_cast<unsigned long>(g_stats.store_bytes), elf_full[0] != '\0' ? elf_full : "N/A");
    ESP_LOGI(STATS_TAG, "legend: cpu%% and core busy are percent of ONE core; busy = 100 - IDLE share; N/A = not available");
    ESP_LOGI(STATS_TAG, "legend: min_boot = lowest free since boot; seen_min = lowest free seen at block samples since start");
    ESP_LOGI(STATS_TAG, "legend: HWM_B = stack high-water mark in bytes; dRun_us = raw runtime delta in the window");
    ESP_LOGI(STATS_TAG, "legend: task names are not read (borrowed TCB pointer is unsafe); Name = T<TCB number>, IDLE<core> via kernel idle handle");

    BROOKESIA_CHECK_FALSE_RETURN(
        config_.task_scheduler->post_periodic(stats_tick, static_cast<int>(STATS_PERIOD_MS)), false,
        "Failed to schedule SYSTEM STATS"
    );
    g_stats.started = true;
    return true;
}

#else

bool Profiler::start_system_stats()
{
    return false;
}

#endif // CONFIG_EXAMPLE_SYSTEM_STATS
