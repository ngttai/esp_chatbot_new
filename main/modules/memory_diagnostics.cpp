/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "modules/memory_diagnostics.h"

#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_CPU && !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
#error "Memory diagnostics CPU statistics require the ESP timer run time stats clock"
#endif

namespace {

constexpr const char *TAG = "memdiag";
constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t DMA_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
constexpr uint32_t SPIRAM_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

constexpr size_t LABEL_SIZE = 32;
constexpr size_t QUEUE_DEPTH = 16;
constexpr size_t MAX_PENDING = 4;
constexpr size_t RECENT_LABELS = 8;
constexpr size_t MAX_GONE = 8;
constexpr uint32_t WORKER_STACK_BYTES = 6 * 1024;
constexpr UBaseType_t WORKER_PRIORITY = 1;
constexpr int64_t COALESCE_US = 2000000;
constexpr int64_t FULL_MIN_INTERVAL_US = 2000000;
constexpr int64_t DROP_WARN_INTERVAL_US = 5000000;
constexpr int64_t PERIOD_US = static_cast<int64_t>(CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_PERIOD_MS) * 1000;
constexpr size_t INITIAL_TASK_CAPACITY = 64;
constexpr size_t TASK_CAPACITY_MARGIN = 8;
constexpr int SNAPSHOT_ATTEMPTS = 3;
constexpr int CORES = portNUM_PROCESSORS;

using RunTime = configRUN_TIME_COUNTER_TYPE;

enum class Kind : uint8_t {
    Heap,
    Full,
};

struct Request {
    int64_t event_us;
    uint32_t delay_ms;
    Kind kind;
    char label[LABEL_SIZE];
};

struct Pending {
    int64_t due_us;
    bool used;
    Request request;
};

struct Recent {
    int64_t event_us;
    char label[LABEL_SIZE];
};

#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS
struct PrevEntry {
    UBaseType_t id;
    RunTime runtime;
    bool matched;
    char name[configMAX_TASK_NAME_LEN];
};

struct Row {
    RunTime delta;
    bool is_new;
    char name[configMAX_TASK_NAME_LEN];
};

struct Gone {
    UBaseType_t id;
    char name[configMAX_TASK_NAME_LEN];
};
#endif

// All worker state lives in one PSRAM block owned by the worker task.
struct WorkerState {
    Pending pending[MAX_PENDING];
    Recent recent[RECENT_LABELS];
    size_t recent_next;
    int64_t last_full_us;
    int64_t last_drop_warn_us;
    uint32_t reported_drops;
    uint32_t coalesced;
    uint32_t downgraded;
#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS
    TaskStatus_t *cur;
    PrevEntry *prev;
    Row *rows;
    size_t capacity;
    size_t prev_count;
    bool have_prev;
    RunTime prev_total;
    int64_t prev_us;
    TaskHandle_t idle[CORES];
    Gone gone[MAX_GONE];
#endif
};

QueueHandle_t s_queue = nullptr;
WorkerState *s_state = nullptr;
std::atomic<uint32_t> s_dropped{0};

unsigned to_ms(int64_t us)
{
    return static_cast<unsigned>(us / 1000);
}

void copy_label(char *dst, const char *src)
{
    const char *input = (src != nullptr) ? src : "unnamed";
    size_t i = 0;
    for (; i + 1 < LABEL_SIZE && input[i] != '\0'; ++i) {
        const char c = input[i];
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                           c == '_' || c == '-' || c == '.' || c == ':';
        dst[i] = valid ? c : '_';
    }
    dst[i] = '\0';
}

void log_heap(const char *label, int64_t event_us, int64_t process_us)
{
    multi_heap_info_t internal{};
    multi_heap_info_t spiram{};
    heap_caps_get_info(&internal, INTERNAL_CAPS);
    heap_caps_get_info(&spiram, SPIRAM_CAPS);
    ESP_LOGI(TAG,
             "MEMDIAG heap label=%s t_ms=%u lag_ms=%u internal_free=%u internal_min=%u internal_largest=%u "
             "dma_largest=%u psram_free=%u psram_min=%u psram_largest=%u",
             label, to_ms(event_us), to_ms(process_us - event_us), static_cast<unsigned>(internal.total_free_bytes),
             static_cast<unsigned>(internal.minimum_free_bytes), static_cast<unsigned>(internal.largest_free_block),
             static_cast<unsigned>(heap_caps_get_largest_free_block(DMA_CAPS)),
             static_cast<unsigned>(spiram.total_free_bytes), static_cast<unsigned>(spiram.minimum_free_bytes),
             static_cast<unsigned>(spiram.largest_free_block));
}

#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS

void format_pct(char *buf, size_t size, bool valid, uint32_t x100)
{
    if (!valid) {
        snprintf(buf, size, "n/a");
        return;
    }
    if (x100 > 10000) {
        x100 = 10000;
    }
    snprintf(buf, size, "%u.%02u", static_cast<unsigned>(x100 / 100), static_cast<unsigned>(x100 % 100));
}

uint32_t ratio_x100(uint64_t part, uint64_t whole)
{
    if (whole == 0) {
        return 0;
    }
    const uint64_t value = (part * 10000ULL) / whole;
    return value > 10000ULL ? 10000U : static_cast<uint32_t>(value);
}

// Grows the PSRAM task tables; the old tables stay valid when allocation fails.
bool reserve_tasks(WorkerState &w, size_t wanted)
{
    if (wanted <= w.capacity) {
        return true;
    }
    const size_t capacity = (wanted + 15) & ~static_cast<size_t>(15);
    auto *cur = static_cast<TaskStatus_t *>(heap_caps_malloc(capacity * sizeof(TaskStatus_t), SPIRAM_CAPS));
    auto *prev = static_cast<PrevEntry *>(heap_caps_malloc(capacity * sizeof(PrevEntry), SPIRAM_CAPS));
    auto *rows = static_cast<Row *>(heap_caps_malloc(capacity * sizeof(Row), SPIRAM_CAPS));
    if (cur == nullptr || prev == nullptr || rows == nullptr) {
        heap_caps_free(cur);
        heap_caps_free(prev);
        heap_caps_free(rows);
        return false;
    }
    if (w.prev_count > 0) {
        memcpy(prev, w.prev, w.prev_count * sizeof(PrevEntry));
    }
    heap_caps_free(w.cur);
    heap_caps_free(w.prev);
    heap_caps_free(w.rows);
    w.cur = cur;
    w.prev = prev;
    w.rows = rows;
    w.capacity = capacity;
    return true;
}

// uxTaskGetSystemState() returns 0 when tasks were created after the size query, so retry with a larger table.
bool collect_tasks(WorkerState &w, const char *label, size_t &count, RunTime &total)
{
    for (int attempt = 0; attempt < SNAPSHOT_ATTEMPTS; ++attempt) {
        const size_t wanted = static_cast<size_t>(uxTaskGetNumberOfTasks()) + TASK_CAPACITY_MARGIN * (attempt + 1);
        if (!reserve_tasks(w, wanted)) {
            ESP_LOGW(TAG, "MEMDIAG warn label=%s task_snapshot_failed reason=alloc_failed", label);
            return false;
        }
        total = 0;
        count = static_cast<size_t>(uxTaskGetSystemState(w.cur, static_cast<UBaseType_t>(w.capacity), &total));
        if (count > 0) {
            return true;
        }
    }
    ESP_LOGW(TAG, "MEMDIAG warn label=%s task_snapshot_failed reason=capacity_race", label);
    return false;
}

#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_CPU
PrevEntry *find_prev(WorkerState &w, UBaseType_t id)
{
    for (size_t i = 0; i < w.prev_count; ++i) {
        if (w.prev[i].id == id) {
            return &w.prev[i];
        }
    }
    return nullptr;
}
#endif

RunTime runtime_of(const TaskStatus_t &task)
{
#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_CPU
    return task.ulRunTimeCounter;
#else
    (void)task;
    return 0;
#endif
}

const char *stack_region(const void *stack_base)
{
    if (esp_ptr_internal(stack_base)) {
        return "internal";
    }
    if (esp_ptr_external_ram(stack_base)) {
        return "psram";
    }
    return "unknown";
}

void log_task_snapshot(WorkerState &w, const char *label)
{
    const int64_t start_us = esp_timer_get_time();
    size_t count = 0;
    RunTime total = 0;
    if (!collect_tasks(w, label, count, total)) {
        ESP_LOGI(TAG,
                 "MEMDIAG cpu label=%s valid=0 window_ms=n/a system_busy_pct=n/a cpu0_busy_pct=n/a "
                 "cpu1_busy_pct=n/a diag_cpu_pct=n/a snapshot_us=n/a log_us=n/a tasks=0 tasks_new=0 "
                 "tasks_deleted=0 dropped=%u coalesced=%u downgraded=%u",
                 label, static_cast<unsigned>(s_dropped.load()), static_cast<unsigned>(w.coalesced),
                 static_cast<unsigned>(w.downgraded));
        return;
    }

    // Copy names immediately; the task may be deleted while the snapshot is being logged.
    for (size_t i = 0; i < count; ++i) {
        Row &row = w.rows[i];
        row.delta = 0;
        row.is_new = false;
        strlcpy(row.name, w.cur[i].pcTaskName, sizeof(row.name));
    }

    bool window_ok = false;
    RunTime total_delta = 0;
    RunTime idle_delta[CORES] = {};
    bool idle_seen[CORES] = {};
    RunTime diag_delta = 0;
    uint32_t tasks_new = 0;
    uint32_t tasks_deleted = 0;
    uint32_t tasks_listed = 0;
#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_CPU
    const bool cpu_enabled = true;
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    total_delta = static_cast<RunTime>(total - w.prev_total);
    const int64_t wall_us = start_us - w.prev_us;
    window_ok = w.have_prev && total_delta > 0 && !(sizeof(RunTime) == 4 && wall_us >= 0xFFFFFFFFLL);

    for (size_t i = 0; i < w.prev_count; ++i) {
        w.prev[i].matched = false;
    }
    for (size_t i = 0; i < count; ++i) {
        const TaskStatus_t &task = w.cur[i];
        Row &row = w.rows[i];
        if (task.eCurrentState == eDeleted) {
            continue;
        }
        PrevEntry *prev = find_prev(w, task.xTaskNumber);
        if (prev != nullptr) {
            prev->matched = true;
            row.delta = static_cast<RunTime>(runtime_of(task) - prev->runtime);
        } else {
            row.is_new = w.have_prev;
            row.delta = runtime_of(task);
            if (row.is_new) {
                ++tasks_new;
            }
        }
        for (int core = 0; core < CORES; ++core) {
            if (task.xHandle == w.idle[core]) {
                idle_delta[core] = row.delta;
                idle_seen[core] = true;
            }
        }
        if (task.xHandle == self) {
            diag_delta = row.delta;
        }
    }
    for (size_t i = 0; i < w.prev_count; ++i) {
        if (w.have_prev && !w.prev[i].matched) {
            if (tasks_deleted < MAX_GONE) {
                w.gone[tasks_deleted].id = w.prev[i].id;
                memcpy(w.gone[tasks_deleted].name, w.prev[i].name, sizeof(w.gone[tasks_deleted].name));
            }
            ++tasks_deleted;
        }
    }
#else
    const bool cpu_enabled = false;
#endif
    const int64_t collected_us = esp_timer_get_time();

    char pct[12];
    char delta_text[12];
    for (size_t i = 0; i < count; ++i) {
        const TaskStatus_t &task = w.cur[i];
        if (task.eCurrentState == eDeleted) {
            continue;
        }
        ++tasks_listed;
        char affinity[16];
        if (task.xCoreID == tskNO_AFFINITY) {
            snprintf(affinity, sizeof(affinity), "any");
        } else {
            snprintf(affinity, sizeof(affinity), "%d", static_cast<int>(task.xCoreID));
        }
        bool has_cpu = false;
        uint32_t cpu_x100 = 0;
        if (cpu_enabled && window_ok) {
            const RunTime delta = w.rows[i].delta;
            snprintf(delta_text, sizeof(delta_text), "%u", static_cast<unsigned>(delta));
            cpu_x100 = ratio_x100(delta, static_cast<uint64_t>(total_delta) * CORES);
            has_cpu = true;
        } else {
            snprintf(delta_text, sizeof(delta_text), "n/a");
        }
        format_pct(pct, sizeof(pct), has_cpu, cpu_x100);
        const bool is_new = cpu_enabled && w.rows[i].is_new;
        ESP_LOGI(TAG,
                 "MEMDIAG task label=%s name=\"%s\" task_id=%u affinity=%s priority=%u stack_region=%s "
                 "hwm_bytes=%u runtime_delta=%s cpu_pct=%s state=%s",
                 label, w.rows[i].name, static_cast<unsigned>(task.xTaskNumber), affinity,
                 static_cast<unsigned>(task.uxCurrentPriority), stack_region(task.pxStackBase),
                 static_cast<unsigned>(task.usStackHighWaterMark), delta_text, pct, is_new ? "new" : "normal");
    }
    for (uint32_t i = 0; i < tasks_deleted && i < MAX_GONE; ++i) {
        ESP_LOGI(TAG, "MEMDIAG task_gone label=%s name=\"%s\" task_id=%u", label, w.gone[i].name,
                 static_cast<unsigned>(w.gone[i].id));
    }

    // Store this snapshot as the baseline for the next window.
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
        const TaskStatus_t &task = w.cur[i];
        if (task.eCurrentState == eDeleted) {
            continue;
        }
        PrevEntry &entry = w.prev[kept++];
        entry.id = task.xTaskNumber;
        entry.runtime = runtime_of(task);
        entry.matched = false;
        strlcpy(entry.name, w.rows[i].name, sizeof(entry.name));
    }
    w.prev_count = kept;
    w.prev_total = total;
    w.prev_us = start_us;
    w.have_prev = true;

    char window[12];
    char system_busy[12];
    char busy[2][12];
    char diag_cpu[12];
    if (cpu_enabled && window_ok) {
        snprintf(window, sizeof(window), "%u", to_ms(static_cast<int64_t>(total_delta)));
        RunTime idle_sum = 0;
        bool all_idle_seen = true;
        for (int core = 0; core < CORES; ++core) {
            idle_sum += idle_delta[core];
            all_idle_seen = all_idle_seen && idle_seen[core];
        }
        format_pct(system_busy, sizeof(system_busy), all_idle_seen,
                   10000 - ratio_x100(idle_sum, static_cast<uint64_t>(total_delta) * CORES));
        for (int core = 0; core < 2; ++core) {
            const bool known = core < CORES && idle_seen[core];
            format_pct(busy[core], sizeof(busy[core]), known,
                       known ? 10000 - ratio_x100(idle_delta[core], total_delta) : 0);
        }
        format_pct(diag_cpu, sizeof(diag_cpu), true,
                   ratio_x100(diag_delta, static_cast<uint64_t>(total_delta) * CORES));
    } else {
        snprintf(window, sizeof(window), "n/a");
        snprintf(system_busy, sizeof(system_busy), "n/a");
        snprintf(busy[0], sizeof(busy[0]), "n/a");
        snprintf(busy[1], sizeof(busy[1]), "n/a");
        snprintf(diag_cpu, sizeof(diag_cpu), "n/a");
    }
    const int64_t logged_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "MEMDIAG cpu label=%s valid=%d window_ms=%s system_busy_pct=%s cpu0_busy_pct=%s cpu1_busy_pct=%s "
             "diag_cpu_pct=%s snapshot_us=%u log_us=%u tasks=%u tasks_new=%u tasks_deleted=%u dropped=%u "
             "coalesced=%u downgraded=%u",
             label, (cpu_enabled && window_ok) ? 1 : 0, window, system_busy, busy[0], busy[1], diag_cpu,
             static_cast<unsigned>(collected_us - start_us), static_cast<unsigned>(logged_us - collected_us),
             static_cast<unsigned>(tasks_listed), static_cast<unsigned>(tasks_new),
             static_cast<unsigned>(tasks_deleted), static_cast<unsigned>(s_dropped.load()),
             static_cast<unsigned>(w.coalesced), static_cast<unsigned>(w.downgraded));
}

#endif // CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS

bool is_duplicate(const WorkerState &w, const Request &request)
{
    for (const Recent &recent : w.recent) {
        if (recent.label[0] != '\0' && strcmp(recent.label, request.label) == 0 &&
                request.event_us - recent.event_us < COALESCE_US) {
            return true;
        }
    }
    return false;
}

void remember(WorkerState &w, const Request &request)
{
    Recent &slot = w.recent[w.recent_next];
    w.recent_next = (w.recent_next + 1) % RECENT_LABELS;
    slot.event_us = request.event_us;
    memcpy(slot.label, request.label, sizeof(slot.label));
}

void process(WorkerState &w, const Request &request, bool forced)
{
    if (!forced && is_duplicate(w, request)) {
        ++w.coalesced;
        return;
    }
    remember(w, request);

    const int64_t now_us = esp_timer_get_time();
    Kind kind = request.kind;
#if !CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS
    kind = Kind::Heap;
#endif
    if (kind == Kind::Full && !forced && now_us - w.last_full_us < FULL_MIN_INTERVAL_US) {
        kind = Kind::Heap;
        ++w.downgraded;
    }

    log_heap(request.label, request.event_us, now_us);
#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS
    if (kind == Kind::Full) {
        log_task_snapshot(w, request.label);
        w.last_full_us = esp_timer_get_time();
    }
#endif
}

void make_request(Request &request, const char *label, Kind kind, int64_t event_us)
{
    memset(&request, 0, sizeof(request));
    request.event_us = event_us;
    request.kind = kind;
    copy_label(request.label, label);
}

bool add_pending(WorkerState &w, const Request &request)
{
    for (Pending &slot : w.pending) {
        if (!slot.used) {
            slot.used = true;
            slot.due_us = request.event_us;
            slot.request = request;
            slot.request.delay_ms = 0;
            return true;
        }
    }
    return false;
}

void report_drops(WorkerState &w, int64_t now_us)
{
    const uint32_t dropped = s_dropped.load();
    if (dropped != w.reported_drops && now_us - w.last_drop_warn_us >= DROP_WARN_INTERVAL_US) {
        ESP_LOGW(TAG, "MEMDIAG warn queue_full dropped=%u", static_cast<unsigned>(dropped));
        w.reported_drops = dropped;
        w.last_drop_warn_us = now_us;
    }
}

void worker_task(void *)
{
    WorkerState &w = *s_state;
#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS
    for (int core = 0; core < CORES; ++core) {
        w.idle[core] = xTaskGetIdleTaskHandleForCore(core);
    }
#endif

    int64_t next_periodic_us = (PERIOD_US > 0) ? esp_timer_get_time() + PERIOD_US : INT64_MAX;

    Request baseline;
    make_request(baseline, "diag_baseline", Kind::Full, esp_timer_get_time());
    process(w, baseline, true);

    while (true) {
        int64_t now_us = esp_timer_get_time();
        int64_t due_us = next_periodic_us;
        for (const Pending &slot : w.pending) {
            if (slot.used && slot.due_us < due_us) {
                due_us = slot.due_us;
            }
        }
        if (s_dropped.load() != w.reported_drops) {
            const int64_t warn_due_us = w.last_drop_warn_us + DROP_WARN_INTERVAL_US;
            if (warn_due_us < due_us) {
                due_us = warn_due_us;
            }
        }
        TickType_t wait = portMAX_DELAY;
        if (due_us != INT64_MAX) {
            wait = (due_us <= now_us) ? 0 : pdMS_TO_TICKS((due_us - now_us + 999) / 1000) + 1;
        }

        Request request;
        if (xQueueReceive(s_queue, &request, wait) == pdTRUE) {
            if (request.delay_ms > 0) {
                if (!add_pending(w, request)) {
                    s_dropped.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                process(w, request, false);
            }
        }

        now_us = esp_timer_get_time();
        for (Pending &slot : w.pending) {
            if (slot.used && slot.due_us <= now_us) {
                slot.used = false;
                process(w, slot.request, false);
            }
        }
        if (next_periodic_us != INT64_MAX && now_us >= next_periodic_us) {
            Request periodic;
            make_request(periodic, "periodic", Kind::Full, next_periodic_us);
            process(w, periodic, true);
            next_periodic_us = esp_timer_get_time() + PERIOD_US;
        }
        report_drops(w, esp_timer_get_time());
    }
}

void enqueue(const char *label, Kind kind, uint32_t delay_ms)
{
    QueueHandle_t queue = s_queue;
    if (queue == nullptr) {
        return;
    }
    Request request;
    memset(&request, 0, sizeof(request));
    request.event_us = esp_timer_get_time() + static_cast<int64_t>(delay_ms) * 1000;
    request.delay_ms = delay_ms;
    request.kind = kind;
    copy_label(request.label, label);
    if (xQueueSend(queue, &request, 0) != pdTRUE) {
        s_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void release_state(WorkerState *state)
{
    if (state == nullptr) {
        return;
    }
#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS
    heap_caps_free(state->cur);
    heap_caps_free(state->prev);
    heap_caps_free(state->rows);
#endif
    heap_caps_free(state);
}

} // namespace

extern "C" void memory_diag_checkpoint(const char *label)
{
    enqueue(label, Kind::Heap, 0);
}

extern "C" void memory_diag_checkpoint_full(const char *label)
{
    enqueue(label, Kind::Full, 0);
}

extern "C" void memory_diag_checkpoint_after(const char *label, uint32_t delay_ms)
{
    enqueue(label, Kind::Full, delay_ms);
}

extern "C" void memory_diag_start(void)
{
    if (s_queue != nullptr) {
        return;
    }
    const int64_t pre_diag_us = esp_timer_get_time();
    log_heap("app_main_pre_diag", pre_diag_us, pre_diag_us);

    auto *state = static_cast<WorkerState *>(heap_caps_calloc(1, sizeof(WorkerState), SPIRAM_CAPS));
    if (state == nullptr) {
        ESP_LOGW(TAG, "MEMDIAG warn diagnostics_start_failed reason=state_alloc");
        return;
    }
#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS_TASKS
    if (!reserve_tasks(*state, INITIAL_TASK_CAPACITY)) {
        ESP_LOGW(TAG, "MEMDIAG warn diagnostics_start_failed reason=task_table_alloc");
        release_state(state);
        return;
    }
#endif
    QueueHandle_t queue = xQueueCreate(QUEUE_DEPTH, sizeof(Request));
    if (queue == nullptr) {
        ESP_LOGW(TAG, "MEMDIAG warn diagnostics_start_failed reason=queue_alloc");
        release_state(state);
        return;
    }

    s_state = state;
    s_queue = queue;
    TaskHandle_t worker = nullptr;
    if (xTaskCreateWithCaps(worker_task, "mem_diag", WORKER_STACK_BYTES, nullptr, WORKER_PRIORITY, &worker,
                            SPIRAM_CAPS) != pdPASS) {
        ESP_LOGW(TAG, "MEMDIAG warn diagnostics_start_failed reason=task_create");
        s_queue = nullptr;
        s_state = nullptr;
        vQueueDelete(queue);
        release_state(state);
        return;
    }

    const int64_t diag_ready_us = esp_timer_get_time();
    log_heap("diag_ready", diag_ready_us, diag_ready_us);
}

#endif // CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS
