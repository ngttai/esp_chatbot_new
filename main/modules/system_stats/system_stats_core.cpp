/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "system_stats_core.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace system_stats {

namespace {

bool same_task(const RawTask &a, const RawTask &b)
{
    return (a.handle == b.handle) && (std::strncmp(a.name, b.name, kTaskNameLen) == 0);
}

// Returns the core an IDLE task belongs to, or -1 when the task is not an IDLE task.
int idle_core_of(const char *name, uint32_t cores)
{
    if (cores == 1) {
        return (std::strncmp(name, "IDLE", kTaskNameLen) == 0) ? 0 : -1;
    }
    if (std::strncmp(name, "IDLE0", kTaskNameLen) == 0) {
        return 0;
    }
    if (std::strncmp(name, "IDLE1", kTaskNameLen) == 0) {
        return 1;
    }
    return -1;
}

uint32_t share_x100(uint32_t delta, uint32_t elapsed)
{
    const uint64_t scaled = (static_cast<uint64_t>(delta) * 10000ULL + elapsed / 2U) / elapsed;
    return static_cast<uint32_t>(std::min<uint64_t>(scaled, 10000ULL));
}

void copy_name(char *dst, const char *src)
{
    const size_t n = strnlen(src, kTaskNameLen - 1);
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}

unsigned long ul(size_t v)
{
    return static_cast<unsigned long>(v);
}

const char *or_na(const char *s)
{
    return ((s != nullptr) && (s[0] != '\0')) ? s : "N/A";
}

void fmt_pct(char *buf, size_t len, bool valid, uint32_t x100)
{
    if (!valid) {
        std::snprintf(buf, len, "N/A");
        return;
    }
    std::snprintf(buf, len, "%lu.%02lu", static_cast<unsigned long>(x100 / 100U),
                  static_cast<unsigned long>(x100 % 100U));
}

void fmt_size(char *buf, size_t len, bool valid, size_t value)
{
    if (!valid) {
        std::snprintf(buf, len, "N/A");
        return;
    }
    std::snprintf(buf, len, "%lu", ul(value));
}

const char *core_text(int8_t core, char *buf, size_t len)
{
    if (core < 0) {
        return "Any";
    }
    std::snprintf(buf, len, "%d", static_cast<int>(core));
    return buf;
}

int sort_group(int8_t core)
{
    return (core < 0) ? 2 : core;
}

bool row_before(const TaskRow &a, const TaskRow &b)
{
    const int ga = sort_group(a.core);
    const int gb = sort_group(b.core);
    if (ga != gb) {
        return ga < gb;
    }
    if (a.cpu_valid != b.cpu_valid) {
        return a.cpu_valid;
    }
    if (a.cpu_valid && (a.cpu_x100 != b.cpu_x100)) {
        return a.cpu_x100 > b.cpu_x100;
    }
    return std::strncmp(a.name, b.name, kTaskNameLen) < 0;
}

void put(LineSink sink, void *user, int level, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

void put(LineSink sink, void *user, int level, const char *fmt, ...)
{
    char line[224];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    sink(user, level, line);
}

bool free_at_or_below(const HeapRegion &r, size_t percent)
{
    return (r.total > 0) &&
           (static_cast<uint64_t>(r.free) * 100ULL <= static_cast<uint64_t>(percent) * static_cast<uint64_t>(r.total));
}

} // namespace

const char *window_status_name(WindowStatus status)
{
    switch (status) {
    case WindowStatus::Ok: return "OK";
    case WindowStatus::NoBaseline: return "NO_BASELINE";
    case WindowStatus::SampleError: return "SAMPLE_ERROR";
    case WindowStatus::ZeroElapsed: return "ZERO_ELAPSED";
    case WindowStatus::ElapsedSkew: return "ELAPSED_SKEW";
    case WindowStatus::WindowTooLong: return "WINDOW_TOO_LONG";
    }
    return "N/A";
}

const char *task_state_name(TaskState state)
{
    switch (state) {
    case TaskState::Running: return "Running";
    case TaskState::Ready: return "Ready";
    case TaskState::Blocked: return "Blocked";
    case TaskState::Suspended: return "Suspended";
    case TaskState::Deleted: return "Deleted";
    case TaskState::Unknown: break;
    }
    return "N/A";
}

const char *stack_loc_name(StackLoc loc)
{
    switch (loc) {
    case StackLoc::Sram: return "SRAM";
    case StackLoc::Psram: return "PSRAM";
    case StackLoc::Unknown: break;
    }
    return "N/A";
}

void compute_cpu_window(const RawSample &start, const RawSample &end, uint32_t cores, CpuWindow &out)
{
    std::memset(&out, 0, sizeof(out));
    out.cores = std::clamp<uint32_t>(cores, 1U, static_cast<uint32_t>(kMaxCores));
    out.status = WindowStatus::SampleError;
    if (!end.valid) {
        return;
    }

    out.live_tasks = static_cast<uint16_t>(std::min<uint32_t>(end.live_tasks, 0xFFFFU));
    out.truncated = (end.live_tasks > end.count) || (start.valid && (start.live_tasks > start.count));
    out.rows = static_cast<uint16_t>(std::min<size_t>(end.count, kMaxTasks));
    for (size_t i = 0; i < out.rows; ++i) {
        const RawTask &t = end.tasks[i];
        TaskRow &r = out.row[i];
        copy_name(r.name, t.name);
        r.handle = t.handle;
        r.core = t.core;
        r.priority = t.priority;
        r.hwm_bytes = t.hwm_bytes;
        r.hwm_valid = t.hwm_valid;
        r.stack = t.stack;
        r.state = t.state;
    }

    if (!start.valid) {
        out.status = WindowStatus::NoBaseline;
        return;
    }

    const uint32_t elapsed = end.total_runtime - start.total_runtime;
    const int64_t mono = end.mono_us - start.mono_us;
    out.elapsed_runtime_us = elapsed;
    out.elapsed_mono_us = mono;
    if (mono <= 0) {
        out.status = WindowStatus::ZeroElapsed;
        return;
    }
    if (mono > kMaxWindowUs) {
        out.status = WindowStatus::WindowTooLong;
        return;
    }
    if (elapsed == 0) {
        out.status = WindowStatus::ZeroElapsed;
        return;
    }
    const int64_t diff = std::max<int64_t>(static_cast<int64_t>(elapsed) - mono, mono - static_cast<int64_t>(elapsed));
    if (diff * 1000 > mono * static_cast<int64_t>(kElapsedSkewPermille)) {
        out.status = WindowStatus::ElapsedSkew;
        return;
    }
    out.status = WindowStatus::Ok;

    // A single task cannot run longer than the window (plus clock read tolerance).
    const uint64_t max_task_delta = static_cast<uint64_t>(elapsed) + elapsed / 100U + 2000U;
    for (size_t i = 0; i < out.rows; ++i) {
        TaskRow &r = out.row[i];
        const RawTask &now = end.tasks[i];
        const RawTask *before = nullptr;
        for (size_t j = 0; j < start.count; ++j) {
            if (same_task(start.tasks[j], now)) {
                before = &start.tasks[j];
                break;
            }
        }
        if (before == nullptr) {
            r.is_new = true;
            ++out.created;
            continue;
        }
        const uint32_t delta = now.runtime - before->runtime;
        if (delta > max_task_delta) {
            ++out.invalid_rows;
            continue;
        }
        r.run_delta_us = delta;
        r.cpu_x100 = share_x100(delta, elapsed);
        r.cpu_valid = true;
    }
    for (size_t j = 0; j < start.count; ++j) {
        bool still_there = false;
        for (size_t i = 0; i < end.count; ++i) {
            if (same_task(start.tasks[j], end.tasks[i])) {
                still_there = true;
                break;
            }
        }
        if (!still_there) {
            ++out.gone;
        }
    }

    for (size_t i = 0; i < out.rows; ++i) {
        const TaskRow &r = out.row[i];
        const int core = idle_core_of(r.name, out.cores);
        if ((core < 0) || !r.cpu_valid || (r.core != core)) {
            continue;
        }
        out.core[core].valid = true;
        out.core[core].idle_delta_us = r.run_delta_us;
        out.core[core].busy_x100 = 10000U - r.cpu_x100;
    }
}

uint32_t busy_warning_mask(const CpuWindow &window)
{
    uint32_t mask = 0;
    if (window.status != WindowStatus::Ok) {
        return 0;
    }
    for (uint32_t c = 0; c < window.cores; ++c) {
        if (window.core[c].valid && (window.core[c].busy_x100 >= kBusyWarnX100)) {
            mask |= (1U << c);
        }
    }
    return mask;
}

void update_mem_seen(MemSeen &seen, const MemorySnapshot &mem)
{
    if (mem.sram.valid) {
        seen.sram_min_free = seen.sram_valid ? std::min(seen.sram_min_free, mem.sram.free) : mem.sram.free;
        seen.sram_valid = true;
    }
    if (mem.psram.valid) {
        seen.psram_min_free = seen.psram_valid ? std::min(seen.psram_min_free, mem.psram.free) : mem.psram.free;
        seen.psram_valid = true;
    }
}

static bool text_differs(const char *a, const char *b)
{
    if ((a == nullptr) || (b == nullptr)) {
        return a != b;
    }
    return std::strcmp(a, b) != 0;
}

uint32_t context_mix_mask(const ContextInfo &ctx)
{
    uint32_t mask = 0;
    if (ctx.screen_changes != 0U) {
        mask |= kMixScreen;
    }
    if (ctx.power_changes != 0U) {
        mask |= kMixPower;
    }
    if (text_differs(ctx.wifi_start, ctx.wifi)) {
        mask |= kMixWifi;
    }
    if (text_differs(ctx.agent_start, ctx.agent)) {
        mask |= kMixAgent;
    }
    return mask;
}

void emit_block(const Block &b, LineSink sink, void *user)
{
    const CpuWindow *cpu = b.cpu;
    const bool cpu_ok = (cpu != nullptr) && (cpu->status == WindowStatus::Ok);

    // Warnings come first so a log filter on WARN sees the cause before the table.
    if (cpu_ok) {
        const uint32_t mask = busy_warning_mask(*cpu);
        for (uint32_t c = 0; c < cpu->cores; ++c) {
            if ((mask & (1U << c)) != 0) {
                put(sink, user, 1,
                    "SYSTEM STATS WARN core%lu busy=%lu.%02lu%% >= %lu%% window_us=%lu (full task table follows)",
                    static_cast<unsigned long>(c), static_cast<unsigned long>(cpu->core[c].busy_x100 / 100U),
                    static_cast<unsigned long>(cpu->core[c].busy_x100 % 100U),
                    static_cast<unsigned long>(kBusyWarnX100 / 100U), static_cast<unsigned long>(cpu->elapsed_runtime_us));
            }
        }
    }
    if ((cpu == nullptr) || (cpu->status != WindowStatus::Ok)) {
        put(sink, user, 1, "SYSTEM STATS WARN cpu window not usable: %s",
            (cpu == nullptr) ? "N/A" : window_status_name(cpu->status));
    }
    if ((b.mem != nullptr) && (b.thresholds != nullptr)) {
        const struct {
            const char *name;
            const HeapRegion *region;
            size_t largest_bytes;
            size_t free_percent;
        } checks[] = {
            {"SRAM", &b.mem->sram, b.thresholds->sram_largest_bytes, b.thresholds->sram_free_percent},
            {"PSRAM", &b.mem->psram, b.thresholds->psram_largest_bytes, b.thresholds->psram_free_percent},
        };
        for (const auto &c : checks) {
            if (!c.region->valid) {
                continue;
            }
            if (c.region->largest <= c.largest_bytes) {
                put(sink, user, 1, "SYSTEM STATS WARN %s largest=%lu B <= %lu B", c.name, ul(c.region->largest),
                    ul(c.largest_bytes));
            }
            if (free_at_or_below(*c.region, c.free_percent)) {
                put(sink, user, 1, "SYSTEM STATS WARN %s free=%lu B of %lu B <= %lu%%", c.name, ul(c.region->free),
                    ul(c.region->total), ul(c.free_percent));
            }
        }
    }

    const unsigned long up_s = static_cast<unsigned long>(b.uptime_ms / 1000ULL);
    put(sink, user, 0, "SYSTEM STATS begin seq=%lu up=%02lu:%02lu:%02lu up_ms=%llu period_ms=%lu",
        static_cast<unsigned long>(b.seq), up_s / 3600UL, (up_s / 60UL) % 60UL, up_s % 60UL,
        static_cast<unsigned long long>(b.uptime_ms), static_cast<unsigned long>(b.period_ms));
    put(sink, user, 0, "build profile=%s assert=%s idf=%s app=%s commit=%s dirty=%s elf=%s store=%s store_bytes=%lu",
        or_na(b.build.profile), or_na(b.build.assertions), or_na(b.build.idf), or_na(b.build.app_version),
        or_na(b.build.commit), or_na(b.build.dirty), or_na(b.build.elf), or_na(b.store),
        static_cast<unsigned long>(b.store_bytes));

    char screen_from[40] = "";
    char power_from[40] = "";
    char wifi_from[48] = "";
    char agent_from[40] = "";
    if ((b.ctx.screen_start != nullptr) && (b.ctx.screen != nullptr) && (std::strcmp(b.ctx.screen_start, b.ctx.screen) != 0)) {
        std::snprintf(screen_from, sizeof(screen_from), " screen_start=%s", b.ctx.screen_start);
    }
    if ((b.ctx.power_start != nullptr) && (b.ctx.power != nullptr) && (std::strcmp(b.ctx.power_start, b.ctx.power) != 0)) {
        std::snprintf(power_from, sizeof(power_from), " power_start=%s", b.ctx.power_start);
    }
    const uint32_t mix = context_mix_mask(b.ctx);
    if ((mix & kMixWifi) != 0U) {
        std::snprintf(wifi_from, sizeof(wifi_from), " wifi_start=%s", or_na(b.ctx.wifi_start));
    }
    if ((mix & kMixAgent) != 0U) {
        std::snprintf(agent_from, sizeof(agent_from), " agent_start=%s", or_na(b.ctx.agent_start));
    }
    char mixed[40] = "no";
    if (mix != 0U) {
        std::snprintf(mixed, sizeof(mixed), "yes:%s%s%s%s", (mix & kMixScreen) != 0U ? "screen+" : "",
                      (mix & kMixPower) != 0U ? "power+" : "", (mix & kMixWifi) != 0U ? "wifi+" : "",
                      (mix & kMixAgent) != 0U ? "agent+" : "");
        mixed[std::strlen(mixed) - 1U] = '\0'; // drop the trailing '+'
    }
    put(sink, user, 0, "ctx screen=%s%s power=%s%s wifi=%s%s agent=%s%s chg_screen=%lu chg_power=%lu wa_check=endpoints mixed=%s",
        or_na(b.ctx.screen), screen_from, or_na(b.ctx.power), power_from, or_na(b.ctx.wifi), wifi_from,
        or_na(b.ctx.agent), agent_from, static_cast<unsigned long>(b.ctx.screen_changes),
        static_cast<unsigned long>(b.ctx.power_changes), mixed);

    {
        char c0[16];
        char c1[16];
        fmt_pct(c0, sizeof(c0), cpu_ok && cpu->core[0].valid, cpu_ok ? cpu->core[0].busy_x100 : 0);
        fmt_pct(c1, sizeof(c1), cpu_ok && (cpu->cores > 1) && cpu->core[1].valid, cpu_ok ? cpu->core[1].busy_x100 : 0);
        char w[24];
        char m[24];
        if (cpu_ok) {
            std::snprintf(w, sizeof(w), "%lu", static_cast<unsigned long>(cpu->elapsed_runtime_us));
            std::snprintf(m, sizeof(m), "%lld", static_cast<long long>(cpu->elapsed_mono_us));
        } else {
            std::snprintf(w, sizeof(w), "N/A");
            std::snprintf(m, sizeof(m), "N/A");
        }
        char i0[16];
        char i1[16];
        fmt_size(i0, sizeof(i0), cpu_ok && cpu->core[0].valid, cpu_ok ? cpu->core[0].idle_delta_us : 0);
        fmt_size(i1, sizeof(i1), cpu_ok && (cpu->cores > 1) && cpu->core[1].valid, cpu_ok ? cpu->core[1].idle_delta_us : 0);
        if ((cpu != nullptr) && (cpu->cores < 2)) {
            put(sink, user, 0, "cpu core0_busy=%s%% unit=pct_of_one_core basis=100-IDLE idle0_us=%s window_us=%s mono_us=%s",
                c0, i0, w, m);
        } else {
            put(sink, user, 0,
                "cpu core0_busy=%s%% core1_busy=%s%% unit=pct_of_one_core basis=100-IDLE idle0_us=%s idle1_us=%s window_us=%s mono_us=%s",
                c0, c1, i0, i1, w, m);
        }
    }

    const MemorySnapshot *mem = b.mem;
    put(sink, user, 0, "mem region %12s %12s %12s %12s %12s", "total_B", "free_B", "min_boot_B", "seen_min_B", "largest_B");
    {
        const struct {
            const char *name;
            const HeapRegion *region;
            bool seen_valid;
            size_t seen_min;
        } rows[] = {
            {"SRAM", (mem != nullptr) ? &mem->sram : nullptr, (b.seen != nullptr) && b.seen->sram_valid,
             (b.seen != nullptr) ? b.seen->sram_min_free : 0},
            {"PSRAM", (mem != nullptr) ? &mem->psram : nullptr, (b.seen != nullptr) && b.seen->psram_valid,
             (b.seen != nullptr) ? b.seen->psram_min_free : 0},
        };
        for (const auto &r : rows) {
            const bool ok = (r.region != nullptr) && r.region->valid;
            char total[24];
            char free_b[24];
            char min_boot[24];
            char seen[24];
            char largest[24];
            fmt_size(total, sizeof(total), ok, ok ? r.region->total : 0);
            fmt_size(free_b, sizeof(free_b), ok, ok ? r.region->free : 0);
            fmt_size(min_boot, sizeof(min_boot), ok, ok ? r.region->min_boot : 0);
            fmt_size(seen, sizeof(seen), ok && r.seen_valid, r.seen_min);
            fmt_size(largest, sizeof(largest), ok, ok ? r.region->largest : 0);
            put(sink, user, 0, "mem %-6s %12s %12s %12s %12s %12s", r.name, total, free_b, min_boot, seen, largest);
        }
    }
    {
        const bool ok = (mem != nullptr) && mem->dma.valid;
        char largest[24];
        char free_b[24];
        fmt_size(largest, sizeof(largest), ok, ok ? mem->dma.largest : 0);
        fmt_size(free_b, sizeof(free_b), ok, ok ? mem->dma.free : 0);
        put(sink, user, 0, "dma largest_B=%s free_B=%s caps=INTERNAL|DMA|8BIT (inside SRAM, not added)", largest, free_b);
    }

    char total_text[16];
    if ((cpu != nullptr) && (cpu->status != WindowStatus::SampleError)) {
        std::snprintf(total_text, sizeof(total_text), "%lu", static_cast<unsigned long>(cpu->live_tasks));
    } else {
        std::snprintf(total_text, sizeof(total_text), "N/A");
    }
    put(sink, user, 0, "tasks total=%s shown=%lu sample=%s trunc=%c new=%lu gone=%lu bad=%lu", total_text,
        static_cast<unsigned long>((cpu != nullptr) ? cpu->rows : 0U),
        (cpu != nullptr) ? window_status_name(cpu->status) : "N/A", ((cpu != nullptr) && cpu->truncated) ? 'Y' : 'N',
        static_cast<unsigned long>((cpu != nullptr) ? cpu->created : 0U),
        static_cast<unsigned long>((cpu != nullptr) ? cpu->gone : 0U),
        static_cast<unsigned long>((cpu != nullptr) ? cpu->invalid_rows : 0U));

    if ((cpu != nullptr) && (cpu->rows > 0)) {
        put(sink, user, 0, "tcol %-16s %-8s %-4s %3s %6s %10s %-5s %6s %-9s %s", "Name", "Hdl", "Core", "Pri", "CPU%",
            "dRun_us", "Stack", "HWM_B", "State", "Note");
        uint8_t order[kMaxTasks];
        for (size_t i = 0; i < cpu->rows; ++i) {
            size_t pos = i;
            while ((pos > 0) && row_before(cpu->row[i], cpu->row[order[pos - 1]])) {
                order[pos] = order[pos - 1];
                --pos;
            }
            order[pos] = static_cast<uint8_t>(i);
        }
        for (size_t k = 0; k < cpu->rows; ++k) {
            const TaskRow &r = cpu->row[order[k]];
            char core_buf[8];
            char cpu_buf[16];
            char run_buf[16];
            char hwm_buf[16];
            fmt_pct(cpu_buf, sizeof(cpu_buf), r.cpu_valid && cpu_ok, r.cpu_x100);
            fmt_size(run_buf, sizeof(run_buf), r.cpu_valid && cpu_ok, r.run_delta_us);
            fmt_size(hwm_buf, sizeof(hwm_buf), r.hwm_valid, r.hwm_bytes);
            const char *note = r.is_new ? "NEW" : ((cpu_ok && !r.cpu_valid) ? "BAD" : "-");
            put(sink, user, 0, "task %-16s %08lx %-4s %3lu %6s %10s %-5s %6s %-9s %s", r.name,
                static_cast<unsigned long>(r.handle), core_text(r.core, core_buf, sizeof(core_buf)),
                static_cast<unsigned long>(r.priority), cpu_buf, run_buf, stack_loc_name(r.stack), hwm_buf,
                task_state_name(r.state), note);
        }
    }

    if (b.prev_cost.valid) {
        put(sink, user, 0, "self prev_sample_us=%lu prev_mem_us=%lu prev_emit_us=%lu",
            static_cast<unsigned long>(b.prev_cost.sample_us), static_cast<unsigned long>(b.prev_cost.mem_us),
            static_cast<unsigned long>(b.prev_cost.emit_us));
    } else {
        put(sink, user, 0, "self prev_sample_us=N/A prev_mem_us=N/A prev_emit_us=N/A");
    }
    put(sink, user, 0, "SYSTEM STATS end seq=%lu", static_cast<unsigned long>(b.seq));
}

} // namespace system_stats
