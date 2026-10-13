/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cmath>
#include <cstdint>

class LiftToWakeDetector {
public:
    struct Vector { float x; float y; float z; };
    LiftToWakeDetector(float tilt_degrees, uint32_t stable_ms, uint32_t cooldown_ms):
        tilt_cos_(std::cos(tilt_degrees * 0.01745329252F)),
        stable_ms_(stable_ms), cooldown_ms_(cooldown_ms) {}
    void reset() { initialized_ = false; armed_ = false; emitted_ = false; }

    bool update(uint32_t now, Vector acceleration_g)
    {
        const float norm = std::sqrt(dot(acceleration_g, acceleration_g));
        if (!std::isfinite(norm) || norm < 0.8F || norm > 1.2F) {
            initialized_ = false;
            return false;
        }
        const Vector direction{acceleration_g.x / norm, acceleration_g.y / norm, acceleration_g.z / norm};
        if (!initialized_) {
            initialized_ = true;
            candidate_ = direction;
            candidate_since_ = now;
            return false;
        }
        // A stable pose tolerates up to five degrees of movement.
        if (dot(candidate_, direction) < 0.9961947F) {
            candidate_ = direction;
            candidate_since_ = now;
            return false;
        }
        if (!armed_) {
            if (static_cast<uint32_t>(now - candidate_since_) >= 800 &&
                    (!emitted_ || static_cast<uint32_t>(now - last_wake_) >= cooldown_ms_)) {
                baseline_ = direction;
                armed_ = true;
            }
            return false;
        }
        if (dot(baseline_, direction) > tilt_cos_) return false;
        if (static_cast<uint32_t>(now - candidate_since_) < stable_ms_ ||
                (emitted_ && static_cast<uint32_t>(now - last_wake_) < cooldown_ms_)) return false;
        armed_ = false;
        emitted_ = true;
        last_wake_ = now;
        candidate_since_ = now;
        return true;
    }

private:
    static float dot(Vector a, Vector b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
    float tilt_cos_;
    uint32_t stable_ms_;
    uint32_t cooldown_ms_;
    Vector candidate_{};
    Vector baseline_{};
    uint32_t candidate_since_ = 0;
    uint32_t last_wake_ = 0;
    bool initialized_ = false;
    bool armed_ = false;
    bool emitted_ = false;
};
