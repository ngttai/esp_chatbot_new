#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "modules/system_stats/system_stats_core.hpp"

using namespace system_stats;

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "System stats check failed at %d: %s\n", __LINE__, #test); return 1; } } while (false)

namespace {

struct Lines {
    std::vector<std::pair<int, std::string>> v;
    bool has(const char *needle) const
    {
        for (const auto &l : v) {
            if (l.second.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    bool has_sq(const char *needle) const
    {
        const std::string want = squash(needle);
        for (const auto &l : v) {
            if (squash(l.second).find(want) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    static std::string squash(const std::string &in)
    {
        std::string out;
        for (char c : in) {
            if (c == ' ' && !out.empty() && out.back() == ' ') {
                continue;
            }
            out.push_back(c);
        }
        return out;
    }
    int count_prefix(const char *prefix) const
    {
        int n = 0;
        for (const auto &l : v) {
            if (l.second.rfind(prefix, 0) == 0) {
                ++n;
            }
        }
        return n;
    }
    int count(const char *needle) const
    {
        int n = 0;
        for (const auto &l : v) {
            if (l.second.find(needle) != std::string::npos) {
                ++n;
            }
        }
        return n;
    }
    int find(const char *needle) const
    {
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i].second.find(needle) != std::string::npos) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

void sink(void *user, int level, const char *line)
{
    static_cast<Lines *>(user)->v.emplace_back(level, line);
}

RawTask make_task(const char *name, uintptr_t handle, uint32_t runtime, int8_t core, uint32_t prio = 5)
{
    RawTask t{};
    std::snprintf(t.name, sizeof(t.name), "%.15s", name);
    t.handle = handle;
    t.runtime = runtime;
    t.core = core;
    t.priority = prio;
    t.hwm_bytes = 1234;
    t.hwm_valid = true;
    t.state = TaskState::Blocked;
    t.stack = StackLoc::Psram;
    return t;
}

void add(RawSample &s, const RawTask &t)
{
    s.tasks[s.count++] = t;
    s.live_tasks = s.count;
}

RawSample make_sample(uint32_t total, int64_t mono)
{
    RawSample s{};
    s.valid = true;
    s.total_runtime = total;
    s.mono_us = mono;
    return s;
}

// Builds a start/end pair with the given task deltas over a 10 s window.
struct Pair {
    RawSample start;
    RawSample end;
};

Pair base_pair(uint32_t idle0_delta, uint32_t idle1_delta, uint32_t gfx_delta, uint32_t total0 = 1000000)
{
    Pair p{make_sample(total0, 5000000), make_sample(total0 + 10000000U, 15000100)};
    add(p.start, make_task("IDLE0", 0x1000, 100000, 0, 0));
    add(p.start, make_task("IDLE1", 0x1001, 200000, 1, 0));
    add(p.start, make_task("gfx_render", 0x2000, 50000, -1, 10));
    add(p.end, make_task("IDLE0", 0x1000, 100000 + idle0_delta, 0, 0));
    add(p.end, make_task("IDLE1", 0x1001, 200000 + idle1_delta, 1, 0));
    add(p.end, make_task("gfx_render", 0x2000, 50000 + gfx_delta, -1, 10));
    return p;
}

Block make_block(const CpuWindow *cpu, const MemorySnapshot *mem, const MemSeen *seen, const MemThresholds *th)
{
    Block b{};
    b.seq = 7;
    b.uptime_ms = 754321;
    b.period_ms = 10000;
    b.build = {"PERF-O2", "ENABLE", "v6.1", "1.2.0", "5e0f4e9", "N", "0123456789abcdef"};
    b.ctx = {"Home", "Home", "Active", "Active", "Off", "Idle", "Off", "Idle", 0, 0};
    b.store = "PSRAM";
    b.store_bytes = 8704;
    b.cpu = cpu;
    b.mem = mem;
    b.seen = seen;
    b.thresholds = th;
    return b;
}

// Splits a `task` row the way the capture parser does: nine fields from the right.
bool parse_task_row(const std::string &line, std::string &name, std::vector<std::string> &fields)
{
    if (line.rfind("task ", 0) != 0) {
        return false;
    }
    std::string rest = line.substr(5);
    std::vector<std::string> tokens;
    size_t end = rest.size();
    for (int i = 0; i < 9; ++i) {
        while (end > 0 && rest[end - 1] == ' ') {
            --end;
        }
        size_t begin = rest.rfind(' ', end - 1);
        begin = (begin == std::string::npos) ? 0 : begin + 1;
        tokens.insert(tokens.begin(), rest.substr(begin, end - begin));
        end = begin;
    }
    while (end > 0 && rest[end - 1] == ' ') {
        --end;
    }
    name = rest.substr(0, end);
    fields = tokens;
    return true;
}

} // namespace

int main()
{
    static CpuWindow w;

    // One-core percentages: busy comes from IDLE, task share is not halved.
    {
        auto p = base_pair(6848000, 3190000, 1234000);
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.status == WindowStatus::Ok);
        CHECK(w.elapsed_runtime_us == 10000000U);
        CHECK(w.core[0].valid && w.core[0].busy_x100 == 3152);
        CHECK(w.core[1].valid && w.core[1].busy_x100 == 6810);
        CHECK(w.core[0].idle_delta_us == 6848000U && w.core[1].idle_delta_us == 3190000U);
        CHECK(w.rows == 3);
        CHECK(w.row[2].cpu_valid && w.row[2].cpu_x100 == 1234);
        CHECK(w.row[2].run_delta_us == 1234000U);
        CHECK(busy_warning_mask(w) == 0);
    }

    // Rounding happens once from raw counters, never from a rounded percentage.
    {
        auto p = base_pair(5000000, 5000000, 1234567);
        p.end.total_runtime = p.start.total_runtime + 10000001U;
        p.end.mono_us = p.start.mono_us + 10000001;
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.status == WindowStatus::Ok);
        CHECK(w.row[2].cpu_x100 == (1234567ULL * 10000ULL + 10000001ULL / 2U) / 10000001ULL);
        CHECK(w.core[0].busy_x100 == 10000U - ((5000000ULL * 10000ULL + 10000001ULL / 2U) / 10000001ULL));
    }

    // 32-bit runtime counters wrap in the total and in task counters.
    {
        auto p = base_pair(2000000, 9000000, 500000, 0xFFFFD8F0U);
        p.start.tasks[0].runtime = 0xFFFFF000U;
        p.end.tasks[0].runtime = 0xFFFFF000U + 2000000U;
        p.start.tasks[2].runtime = 0xFFFFFF00U;
        p.end.tasks[2].runtime = 0xFFFFFF00U + 500000U;
        CHECK(p.end.total_runtime < p.start.total_runtime);
        CHECK(p.end.tasks[0].runtime < p.start.tasks[0].runtime);
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.status == WindowStatus::Ok);
        CHECK(w.elapsed_runtime_us == 10000000U);
        CHECK(w.core[0].busy_x100 == 8000 && w.core[1].busy_x100 == 1000);
        CHECK(w.row[2].cpu_x100 == 500);
    }

    // Warning threshold: >= 99.00% busy on either core, evaluated per core.
    {
        auto p = base_pair(100000, 5000000, 0);
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.core[0].busy_x100 == 9900);
        CHECK(busy_warning_mask(w) == 0x1);
        p = base_pair(101000, 5000000, 0);
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.core[0].busy_x100 == 9899);
        CHECK(busy_warning_mask(w) == 0);
        p = base_pair(5000000, 50000, 0);
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(busy_warning_mask(w) == 0x2);
        p = base_pair(10000, 10000, 0);
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(busy_warning_mask(w) == 0x3);
        p = base_pair(0, 0, 0);
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.core[0].busy_x100 == 10000 && busy_warning_mask(w) == 0x3);
    }

    // Missing or inconsistent samples never become 0%.
    {
        auto p = base_pair(1000000, 1000000, 10);
        RawSample bad{};
        compute_cpu_window(p.start, bad, 2, w);
        CHECK(w.status == WindowStatus::SampleError && w.rows == 0 && !w.core[0].valid && !w.core[1].valid);
        CHECK(busy_warning_mask(w) == 0);

        compute_cpu_window(bad, p.end, 2, w);
        CHECK(w.status == WindowStatus::NoBaseline && w.rows == 3);
        for (int i = 0; i < 3; ++i) {
            CHECK(!w.row[i].cpu_valid);
        }
        CHECK(!w.core[0].valid && !w.core[1].valid);

        auto z = base_pair(1, 1, 1);
        z.end.total_runtime = z.start.total_runtime;
        compute_cpu_window(z.start, z.end, 2, w);
        CHECK(w.status == WindowStatus::ZeroElapsed && !w.core[0].valid);

        auto skew = base_pair(1, 1, 1);
        skew.end.total_runtime = skew.start.total_runtime + 12000000U;
        compute_cpu_window(skew.start, skew.end, 2, w);
        CHECK(w.status == WindowStatus::ElapsedSkew && !w.core[0].valid && busy_warning_mask(w) == 0);

        auto longw = base_pair(1, 1, 1);
        longw.end.mono_us = longw.start.mono_us + kMaxWindowUs + 1;
        compute_cpu_window(longw.start, longw.end, 2, w);
        CHECK(w.status == WindowStatus::WindowTooLong);

        auto back = base_pair(1, 1, 1);
        back.end.mono_us = back.start.mono_us - 5;
        compute_cpu_window(back.start, back.end, 2, w);
        CHECK(w.status == WindowStatus::ZeroElapsed);
    }

    // Tasks that appear or vanish are reported without inventing a percentage.
    {
        auto p = base_pair(5000000, 5000000, 100000);
        add(p.end, make_task("mqtt_task", 0x3000, 777, 0));
        p.start.tasks[2].handle = 0x2000;
        RawTask gone = make_task("old_job", 0x4000, 10, -1);
        add(p.start, gone);
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.status == WindowStatus::Ok);
        CHECK(w.created == 1 && w.gone == 1 && w.invalid_rows == 0);
        CHECK(w.rows == 4 && w.row[3].is_new && !w.row[3].cpu_valid);
        CHECK(w.core[0].valid && w.core[1].valid);

        // Reused handle with another name counts as one new and one gone task.
        auto r = base_pair(5000000, 5000000, 100000);
        std::snprintf(r.end.tasks[2].name, sizeof(r.end.tasks[2].name), "%s", "other");
        compute_cpu_window(r.start, r.end, 2, w);
        CHECK(w.created == 1 && w.gone == 1 && w.row[2].is_new && !w.row[2].cpu_valid);
    }

    // A counter that went backwards is marked bad; a bad IDLE row disables that core only.
    {
        auto p = base_pair(5000000, 5000000, 100000);
        p.end.tasks[2].runtime = p.start.tasks[2].runtime - 1000;
        p.end.tasks[1].runtime = p.start.tasks[1].runtime + 40000000U;
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.status == WindowStatus::Ok && w.invalid_rows == 2);
        CHECK(!w.row[2].cpu_valid && !w.row[1].cpu_valid && !w.row[2].is_new);
        CHECK(w.core[0].valid && !w.core[1].valid);
    }

    // Missing IDLE task or unexpected affinity gives N/A busy.
    {
        auto p = base_pair(5000000, 5000000, 100000);
        p.end.tasks[1].core = 0;
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.core[0].valid && !w.core[1].valid);
        RawSample only_gfx = make_sample(1000000, 5000000);
        RawSample only_gfx_end = make_sample(11000000, 15000000);
        add(only_gfx, make_task("gfx_render", 0x2000, 0, -1));
        add(only_gfx_end, make_task("gfx_render", 0x2000, 400000, -1));
        compute_cpu_window(only_gfx, only_gfx_end, 2, w);
        CHECK(w.status == WindowStatus::Ok && !w.core[0].valid && !w.core[1].valid && w.row[0].cpu_x100 == 400);
    }

    // Single-core targets use the plain IDLE task name.
    {
        RawSample a = make_sample(0, 1000000);
        RawSample b = make_sample(10000000, 11000000);
        add(a, make_task("IDLE", 0x10, 0, 0, 0));
        add(b, make_task("IDLE", 0x10, 2500000, 0, 0));
        compute_cpu_window(a, b, 1, w);
        CHECK(w.cores == 1 && w.core[0].valid && w.core[0].busy_x100 == 7500);
    }

    // Task cap: scheduler count above stored entries marks the window truncated.
    {
        auto p = base_pair(5000000, 5000000, 1);
        p.end.live_tasks = kMaxTasks + 5;
        compute_cpu_window(p.start, p.end, 2, w);
        CHECK(w.truncated && w.live_tasks == kMaxTasks + 5 && w.rows == 3);
    }

    // Memory minimum observed in the session is separate from the boot minimum.
    {
        MemSeen seen{};
        MemorySnapshot m{};
        m.sram = {true, 300000, 120000, 90000, 60000};
        update_mem_seen(seen, m);
        CHECK(seen.sram_valid && seen.sram_min_free == 120000 && !seen.psram_valid);
        m.sram.free = 150000;
        m.psram = {true, 8000000, 7000000, 6500000, 5000000};
        update_mem_seen(seen, m);
        CHECK(seen.sram_min_free == 120000 && seen.psram_min_free == 7000000);
        m.sram.free = 100000;
        update_mem_seen(seen, m);
        CHECK(seen.sram_min_free == 100000 && m.sram.min_boot == 90000);
        MemorySnapshot invalid{};
        update_mem_seen(seen, invalid);
        CHECK(seen.sram_min_free == 100000);
    }

    // Block format: header, N/A handling, ordering, DMA kept apart, parseable task rows.
    {
        auto p = base_pair(6848000, 3190000, 1234000);
        add(p.start, make_task("Tmr Svc", 0x5000, 100, -1, 1));
        add(p.end, make_task("Tmr Svc", 0x5000, 100 + 2000, -1, 1));
        add(p.start, make_task("lvgl", 0x6000, 0, 1, 6));
        add(p.end, make_task("lvgl", 0x6000, 400000, 1, 6));
        p.end.tasks[2].hwm_valid = false;
        compute_cpu_window(p.start, p.end, 2, w);

        MemorySnapshot mem{};
        mem.sram = {true, 330000, 140000, 89456, 65536};
        mem.psram = {true, 8386608, 8113580, 8000000, 8000000};
        mem.dma = {true, 130000, 65536};
        MemSeen seen{};
        update_mem_seen(seen, mem);
        MemThresholds th{10 * 1024, 10, 500 * 1024, 20};
        Block b = make_block(&w, &mem, &seen, &th);
        b.prev_cost = {true, 120, 80, 45000};
        Lines out;
        emit_block(b, sink, &out);

        CHECK(out.find("SYSTEM STATS begin seq=7 up=00:12:34 up_ms=754321 period_ms=10000") == 0);
        CHECK(out.v.back().second == "SYSTEM STATS end seq=7");
        CHECK(out.has("build profile=PERF-O2 assert=ENABLE idf=v6.1 app=1.2.0 commit=5e0f4e9 dirty=N elf=0123456789abcdef store=PSRAM store_bytes=8704"));
        CHECK(out.has("ctx screen=Home power=Active wifi=Off agent=Idle"));
        CHECK(out.has("core0_busy=31.52% core1_busy=68.10% unit=pct_of_one_core basis=100-IDLE idle0_us=6848000 idle1_us=3190000 window_us=10000000 mono_us=10000100"));
        CHECK(out.has_sq("mem SRAM 330000 140000 89456 140000 65536"));
        CHECK(out.has_sq("mem PSRAM 8386608 8113580 8000000 8113580 8000000"));
        CHECK(out.has("dma largest_B=65536 free_B=130000 caps=INTERNAL|DMA|8BIT (inside SRAM, not added)"));
        CHECK(out.has("tasks total=5 shown=5 sample=OK trunc=N new=0 gone=0 bad=0"));
        CHECK(out.has("self prev_sample_us=120 prev_mem_us=80 prev_emit_us=45000"));
        CHECK(out.count("WARN") == 0);
        for (const auto &l : out.v) {
            CHECK(l.first == 0 && l.second.size() < 200);
        }

        std::vector<std::string> names;
        std::vector<std::string> cpu_col;
        std::vector<std::string> core_col;
        for (const auto &l : out.v) {
            std::string name;
            std::vector<std::string> f;
            if (parse_task_row(l.second, name, f)) {
                CHECK(f.size() == 9);
                names.push_back(name);
                core_col.push_back(f[1]);
                cpu_col.push_back(f[3]);
                if (name == "gfx_render") {
                    CHECK(f[0] == "00002000" && f[1] == "Any" && f[2] == "10" && f[3] == "12.34");
                    CHECK(f[4] == "1234000" && f[5] == "PSRAM" && f[6] == "N/A" && f[7] == "Blocked" && f[8] == "-");
                }
                if (name == "Tmr Svc") {
                    CHECK(f[3] == "0.02" && f[6] == "1234");
                }
            }
        }
        CHECK(names.size() == 5);
        CHECK(names[0] == "IDLE0" && names[1] == "IDLE1" && names[2] == "lvgl" && names[3] == "gfx_render" && names[4] == "Tmr Svc");
        CHECK(core_col[0] == "0" && core_col[1] == "1" && core_col[2] == "1" && core_col[3] == "Any" && core_col[4] == "Any");
        CHECK(cpu_col[1] == "31.90" && cpu_col[2] == "4.00" && cpu_col[3] == "12.34");
    }

    // High-busy window adds a core-specific WARN and still prints the full table.
    {
        auto p = base_pair(50000, 5000000, 9000000);
        compute_cpu_window(p.start, p.end, 2, w);
        Block b = make_block(&w, nullptr, nullptr, nullptr);
        Lines out;
        emit_block(b, sink, &out);
        CHECK(out.count("SYSTEM STATS WARN core0 busy=99.50% >= 99%") == 1);
        CHECK(out.count("WARN core1") == 0);
        const int warn_at = out.find("WARN core0");
        CHECK(out.v[warn_at].first == 1 && warn_at < out.find("SYSTEM STATS begin"));
        CHECK(out.count_prefix("task ") == 3);
        CHECK(out.has("mem SRAM") && out.has("N/A"));
    }

    // Unusable windows print N/A, a single warning, and never a fabricated 0.00.
    {
        RawSample bad{};
        auto p = base_pair(1, 1, 1);
        compute_cpu_window(p.start, bad, 2, w);
        Block b = make_block(&w, nullptr, nullptr, nullptr);
        Lines out;
        emit_block(b, sink, &out);
        CHECK(out.count("WARN cpu window not usable: SAMPLE_ERROR") == 1);
        CHECK(out.has("core0_busy=N/A% core1_busy=N/A%"));
        CHECK(out.has("tasks total=N/A shown=0 sample=SAMPLE_ERROR"));
        CHECK(out.count_prefix("task ") == 0 && !out.has("0.00"));
        CHECK(out.has("self prev_sample_us=N/A"));

        compute_cpu_window(bad, p.end, 2, w);
        Lines out2;
        emit_block(make_block(&w, nullptr, nullptr, nullptr), sink, &out2);
        CHECK(out2.has("sample=NO_BASELINE") && out2.count_prefix("task ") == 3 && !out2.has("0.00"));

        Block empty{};
        Lines out3;
        emit_block(empty, sink, &out3);
        CHECK(out3.has("build profile=N/A") && out3.has("ctx screen=N/A power=N/A wifi=N/A agent=N/A"));
        CHECK(out3.has("tasks total=N/A shown=0 sample=N/A"));
    }

    // New and bad rows are labelled; new tasks show N/A CPU.
    {
        auto p = base_pair(5000000, 5000000, 100000);
        add(p.end, make_task("fresh", 0x7000, 99, 0));
        p.end.tasks[2].runtime = p.start.tasks[2].runtime - 5;
        compute_cpu_window(p.start, p.end, 2, w);
        Lines out;
        emit_block(make_block(&w, nullptr, nullptr, nullptr), sink, &out);
        bool saw_new = false;
        bool saw_bad = false;
        for (const auto &l : out.v) {
            std::string name;
            std::vector<std::string> f;
            if (parse_task_row(l.second, name, f)) {
                if (name == "fresh") {
                    saw_new = (f[3] == "N/A" && f[4] == "N/A" && f[8] == "NEW");
                }
                if (name == "gfx_render") {
                    saw_bad = (f[3] == "N/A" && f[8] == "BAD");
                }
            }
        }
        CHECK(saw_new && saw_bad);
        CHECK(out.has("new=1 gone=0 bad=1"));
    }

    // Memory alerts follow the configured thresholds, inclusive, once per block.
    {
        auto p = base_pair(5000000, 5000000, 1);
        compute_cpu_window(p.start, p.end, 2, w);
        MemorySnapshot mem{};
        mem.sram = {true, 300000, 30000, 20000, 10240};
        mem.psram = {true, 8000000, 1600000, 1500000, 1600000};
        MemThresholds th{10 * 1024, 10, 500 * 1024, 20};
        Lines out;
        emit_block(make_block(&w, &mem, nullptr, &th), sink, &out);
        CHECK(out.count("WARN SRAM largest=10240 B <= 10240 B") == 1);
        CHECK(out.count("WARN SRAM free=30000 B of 300000 B <= 10%") == 1);
        CHECK(out.count("WARN PSRAM free=1600000 B of 8000000 B <= 20%") == 1);
        CHECK(out.count("WARN PSRAM largest") == 0);
        mem.sram = {true, 300000, 30001, 20000, 10241};
        Lines quiet;
        emit_block(make_block(&w, &mem, nullptr, &th), sink, &quiet);
        CHECK(quiet.count("WARN SRAM") == 0);
        mem.sram = {false, 0, 0, 0, 0};
        Lines na;
        emit_block(make_block(&w, &mem, nullptr, &th), sink, &na);
        CHECK(na.count("WARN SRAM") == 0 && na.has_sq("mem SRAM N/A N/A N/A N/A N/A"));
    }

    // Context changes inside the window are visible, never silently merged.
    {
        auto p = base_pair(5000000, 5000000, 1);
        compute_cpu_window(p.start, p.end, 2, w);
        Block b = make_block(&w, nullptr, nullptr, nullptr);
        b.ctx.screen_start = "Launcher";
        b.ctx.power_start = "Dimmed";
        b.ctx.screen_changes = 1;
        b.ctx.power_changes = 1;
        Lines out;
        emit_block(b, sink, &out);
        CHECK(out.has("ctx screen=Home screen_start=Launcher power=Active power_start=Dimmed wifi=Off agent=Idle"));
        CHECK(out.has("chg_screen=1 chg_power=1 wa_check=endpoints mixed=yes:screen+power"));
    }

    // Steady window: nothing changed, so the window is usable as a baseline.
    {
        auto p = base_pair(5000000, 5000000, 1);
        compute_cpu_window(p.start, p.end, 2, w);
        Lines out;
        emit_block(make_block(&w, nullptr, nullptr, nullptr), sink, &out);
        CHECK(out.has("chg_screen=0 chg_power=0 wa_check=endpoints mixed=no"));
        CHECK(out.count("screen_start=") == 0 && out.count("wifi_start=") == 0 && out.count("agent_start=") == 0);
        CHECK(context_mix_mask(make_block(&w, nullptr, nullptr, nullptr).ctx) == 0U);
    }

    // Home -> Launcher -> Home: both ends read "Home", but the revision moved twice, so the window is mixed.
    {
        auto p = base_pair(5000000, 5000000, 1);
        compute_cpu_window(p.start, p.end, 2, w);
        Block b = make_block(&w, nullptr, nullptr, nullptr);
        b.ctx.screen = "Home";
        b.ctx.screen_start = "Home";
        b.ctx.screen_changes = 2;
        Lines out;
        emit_block(b, sink, &out);
        CHECK(out.has("ctx screen=Home power=Active wifi=Off agent=Idle chg_screen=2 chg_power=0 wa_check=endpoints mixed=yes:screen"));
        CHECK(out.count("screen_start=") == 0); // the ends match, only the counter exposes the round trip
        CHECK(context_mix_mask(b.ctx) == kMixScreen);
    }

    // Same for a power round trip (Active -> Dimmed -> Active).
    {
        Block b = make_block(&w, nullptr, nullptr, nullptr);
        b.ctx.power_changes = 2;
        Lines out;
        emit_block(b, sink, &out);
        CHECK(out.has("mixed=yes:power"));
        CHECK(context_mix_mask(b.ctx) == kMixPower);
    }

    // The revision counter wraps modulo 2^32; the caller passes the unsigned difference.
    {
        const uint32_t before = 0xFFFFFFFFU;
        const uint32_t after = 1U;
        Block b = make_block(&w, nullptr, nullptr, nullptr);
        b.ctx.screen_changes = after - before;
        CHECK(b.ctx.screen_changes == 2U);
        CHECK(context_mix_mask(b.ctx) == kMixScreen);
    }

    // Wi-Fi and agent are only compared at the ends: a change is flagged, a round trip is not detectable.
    {
        Block b = make_block(&w, nullptr, nullptr, nullptr);
        b.ctx.wifi = "On,Connected";
        b.ctx.wifi_start = "On,NotConnected";
        b.ctx.agent = "Listening";
        b.ctx.agent_start = "Idle";
        Lines out;
        emit_block(b, sink, &out);
        CHECK(out.has("wifi=On,Connected wifi_start=On,NotConnected agent=Listening agent_start=Idle"));
        CHECK(out.has("mixed=yes:wifi+agent"));
        CHECK(context_mix_mask(b.ctx) == (kMixWifi | kMixAgent));

        Block rt = make_block(&w, nullptr, nullptr, nullptr); // Idle -> Listening -> Idle: invisible at the ends
        Lines out2;
        emit_block(rt, sink, &out2);
        CHECK(out2.has("wa_check=endpoints mixed=no"));
    }

    // An unknown start value (not provided, or agent not initialised yet) is not "steady".
    {
        Block b = make_block(&w, nullptr, nullptr, nullptr);
        b.ctx.agent_start = nullptr;
        Lines out;
        emit_block(b, sink, &out);
        CHECK(out.has("agent_start=N/A") && out.has("mixed=yes:agent"));
        b.ctx.agent = nullptr;
        CHECK((context_mix_mask(b.ctx) & kMixAgent) == 0U); // unknown at both ends: nothing to compare
    }

    // Largest supported table stays within the line limit.
    {
        RawSample a = make_sample(0, 1000000);
        RawSample b = make_sample(10000000, 11000000);
        add(a, make_task("IDLE0", 0x1000, 0, 0, 0));
        add(a, make_task("IDLE1", 0x1001, 0, 1, 0));
        add(b, make_task("IDLE0", 0x1000, 4000000, 0, 0));
        add(b, make_task("IDLE1", 0x1001, 5000000, 1, 0));
        for (size_t i = 2; i < kMaxTasks; ++i) {
            char name[kTaskNameLen];
            std::snprintf(name, sizeof(name), "task_name_%05zu", i);
            add(a, make_task(name, 0x2000 + i, 0, static_cast<int8_t>(i % 3) - 1));
            add(b, make_task(name, 0x2000 + i, static_cast<uint32_t>(i * 1000), static_cast<int8_t>(i % 3) - 1));
        }
        compute_cpu_window(a, b, 2, w);
        CHECK(w.rows == kMaxTasks && !w.truncated);
        Lines out;
        emit_block(make_block(&w, nullptr, nullptr, nullptr), sink, &out);
        CHECK(out.count_prefix("task ") == static_cast<int>(kMaxTasks));
        for (const auto &l : out.v) {
            CHECK(l.second.size() < 200);
        }
    }

    std::printf("System stats passed: context revisions and mixed windows, one-core units, raw rounding, 32-bit wrap, 99%% threshold, failed/new/gone/bad samples, N/A handling, memory minima, alerts, block format and parsing; window=%zu bytes\n", sizeof(CpuWindow));
    return 0;
}
