/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <cstdint>
#include "modules/suspend_coordinator.hpp"

// Single-owner, allocation-free adapter. Backend operations must not wait for render locks.
class RenderSuspend {
public:
    static constexpr uint8_t LVGL_ID = 1, GFX_ID = 2, DISPLAY_ID = 3;
    static constexpr uint8_t MASK = 7;
    struct Backend {
        virtual ~Backend() = default;
        virtual bool admission(bool enabled) = 0;
        virtual bool pause_lvgl(bool paused) = 0;
        virtual bool pause_gfx(bool paused) = 0;
        virtual bool drained() = 0;
        virtual uint32_t active_source() = 0;
        virtual void arm_redraw(uint32_t source) = 0;
        virtual bool redraw_complete() = 0;
        virtual void report(uint32_t session, uint8_t id, SuspendCoordinator::Report status) = 0;
    };

    void update(const SuspendCoordinator::Snapshot &snapshot, Backend &backend)
    {
        using Phase = SuspendCoordinator::Phase;
        using Report = SuspendCoordinator::Report;
        if ((snapshot.expected & MASK) != MASK) return;
        if (snapshot.session != session_ || snapshot.phase != phase_) {
            session_ = snapshot.session;
            phase_ = snapshot.phase;
            reported_ = 0;
            if (phase_ == Phase::Preparing || phase_ == Phase::Resuming) {
                step_ = Step::Close;
                lvgl_paused_ = gfx_paused_ = false;
            }
        }
        if (phase_ == Phase::Preparing) {
            if (step_ == Step::Close) {
                if (!backend.admission(false)) return;
                step_ = Step::Pause;
            }
            if (!lvgl_paused_) lvgl_paused_ = backend.pause_lvgl(true);
            if (!gfx_paused_) gfx_paused_ = backend.pause_gfx(true);
            if (lvgl_paused_) acknowledge(backend, LVGL_ID, Report::Ready);
            if (gfx_paused_) acknowledge(backend, GFX_ID, Report::Ready);
            if (lvgl_paused_ && gfx_paused_ && backend.drained()) {
                acknowledge(backend, DISPLAY_ID, Report::Ready);
            }
        } else if (phase_ == Phase::Committing) {
            const auto status = lvgl_paused_ && gfx_paused_ && backend.drained() ?
                                Report::Suspended : Report::Error;
            for (uint8_t id = LVGL_ID; id <= DISPLAY_ID; ++id) acknowledge(backend, id, status);
        } else if (phase_ == Phase::Resuming) {
            // Fence any partially completed Prepare, including a retry after Recovery.
            if (step_ == Step::Close) {
                if (!backend.admission(false)) return;
                step_ = Step::Pause;
            }
            if (step_ == Step::Pause) {
                if (!lvgl_paused_) lvgl_paused_ = backend.pause_lvgl(true);
                if (!gfx_paused_) gfx_paused_ = backend.pause_gfx(true);
                if (!lvgl_paused_ || !gfx_paused_ || !backend.drained()) return;
                source_ = backend.active_source();
                if (!source_) return;
                backend.arm_redraw(source_);
                if (!backend.admission(true)) return;
                step_ = Step::Resume;
            }
            if (step_ == Step::Resume) {
                if (lvgl_paused_ && backend.pause_lvgl(false)) lvgl_paused_ = false;
                if (gfx_paused_ && backend.pause_gfx(false)) gfx_paused_ = false;
                if (lvgl_paused_ || gfx_paused_) return;
                step_ = Step::Redraw;
                acknowledge(backend, LVGL_ID, Report::Resumed);
                acknowledge(backend, GFX_ID, Report::Resumed);
            }
            if (step_ == Step::Redraw) {
                if (backend.active_source() != source_) {
                    step_ = Step::Close;
                    lvgl_paused_ = gfx_paused_ = false;
                    return;
                }
                if (backend.redraw_complete()) acknowledge(backend, DISPLAY_ID, Report::Resumed);
            }
        }
        // Suspended/Recovery deliberately retain the current gate/render state.
    }

private:
    enum class Step : uint8_t { Close, Pause, Resume, Redraw };
    void acknowledge(Backend &backend, uint8_t id, SuspendCoordinator::Report status)
    {
        const uint8_t bit = 1U << (id - 1);
        if (reported_ & bit) return;
        reported_ |= bit;
        backend.report(session_, id, status);
    }
    uint32_t session_ = 0, source_ = 0;
    SuspendCoordinator::Phase phase_ = SuspendCoordinator::Phase::Idle;
    Step step_ = Step::Close;
    uint8_t reported_ = 0;
    bool lvgl_paused_ = false, gfx_paused_ = false;
};

// Full-width contiguous completed bands cover a forced full redraw. Not a submission counter.
class RedrawFence {
public:
    void arm(uint32_t source, uint32_t width, uint32_t height)
    {
        source_ = source; width_ = width; height_ = height; rows_ = 0;
    }
    void complete(uint32_t source, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        if (source != source_ || !source_ || x != 0 || width != width_ || !height ||
            y > rows_ || y >= height_ || height > height_ - y) return;
        if (y + height > rows_) rows_ = y + height;
    }
    bool done() const { return source_ && height_ && rows_ == height_; }
private:
    uint32_t source_ = 0, width_ = 0, height_ = 0, rows_ = 0;
};
