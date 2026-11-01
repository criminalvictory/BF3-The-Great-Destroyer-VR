#include "pistolslide.h"

#include <algorithm>
#include <cmath>

namespace pistolslide {
namespace {

// A point in the view's space into gun space (the pose's rows: rotation | translation, rigid).
void ToGun(const float* pose, const float* p, float* out) {
    const float d[3] = {p[0] - pose[3], p[1] - pose[7], p[2] - pose[11]};
    for (int i = 0; i < 3; ++i)
        out[i] = pose[i] * d[0] + pose[4 + i] * d[1] + pose[8 + i] * d[2];
}

float Distance(const float* a, const float* b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

}

Output Slide::Update(const Input& in) {
    Output o;
    const float dt = in.dt > 0.0f ? std::min(in.dt, 0.1f) : 0.0f;  // (not a number: none)
    // A fresh press, followed with the pistol in hand or not: a trigger already held as it comes into hand isn't one.
    bool press = false;
    if (in.trigger < 0.4f)
        m_armed = true;
    else if (m_armed && in.trigger > 0.6f)
        press = true, m_armed = false;
    if (!in.pistolPose) {
        // No pistol: let go of, nothing drawn.
        if (m_held) {
            o.letGo = true;
            o.pulled = m_maxPull;
        }
        m_held = m_springing = m_cycleBack = false;
        m_drawn = -1.0f;
        return o;
    }
    float palm[3] = {};
    bool have = false;
    if (in.palm) {
        ToGun(in.pistolPose, in.palm, palm);
        have = std::isfinite(palm[0]) && std::isfinite(palm[1]) && std::isfinite(palm[2]);  // a broken pose: no palm
    }
    // Where it is now: held or springing as the mod draws it, on the stop, or home (the game's own then).
    const float now = m_drawn >= 0.0f ? m_drawn : in.locked ? m_p.lockAt : 0.0f;

    // The press near the hold on its rear, where the slide is now.
    if (press && !m_held) {
        const float at[3] = {m_p.grip[0], m_p.grip[1], m_p.grip[2] - now};
        o.pressed = true;
        o.palmFrom = have ? Distance(palm, at) : -1.0f;
        if (have && o.palmFrom < m_p.reach) {
            m_held = true;
            m_grabZ = palm[2];
            m_start = m_pull = m_maxPull = now;
            m_startLocked = in.locked;
            m_springing = m_cycleBack = false;
            o.grabbed = true;
        }
    }
    if (m_held) {
        if (have && in.trigger >= 0.4f) {
            // As far back as the hand has come since the grab (the frame's -z), on the stop's side of it while locked.
            const float low = m_startLocked ? m_p.lockAt : 0.0f;
            m_pull = std::min(std::max(m_start + (m_grabZ - palm[2]), low), m_p.travel);
            m_maxPull = std::max(m_maxPull, m_pull);
            m_drawn = m_pull;
            o.held = true;
            o.back = m_drawn;
            return o;
        }
        // Let go (or the hand lost): far enough back, it feeds; from the stop, a tug past it drops the stop (the same as
        // a rack then: the chamber is empty on the stop). Either way it springs forward from where it was let go.
        m_held = false;
        o.letGo = true;
        o.pulled = m_maxPull;
        o.racked = m_startLocked ? m_maxPull >= m_p.lockAt + m_p.tug : m_maxPull >= m_p.rackAt;
        m_springing = true;
        m_springFrom = m_pull;
        m_springT = 0.0f;
    } else if (!m_springing && m_drawn >= 0.0f && !in.locked) {
        // On the stop, and the stop dropped: home from there, not in one jump. (A tug off the stop springs it back onto
        // the stop as it's let go, before the caller has told the mechanism, which drops the stop for the next frame.)
        m_springing = true;
        m_springFrom = m_drawn;
        m_springT = 0.0f;
    }
    if (m_springing && m_cycleBack) {
        // The trigger's rack from home: back first (30 ms), as a shot's cycle.
        constexpr float kBack = 0.03f;
        m_springT += dt;
        m_drawn = m_p.travel * std::min(m_springT / kBack, 1.0f);
        if (m_springT >= kBack)
            m_cycleBack = false, m_springT = 0.0f;
        o.back = m_drawn;
        return o;
    }
    if (m_springing) {
        // Home, or onto the stop while the mechanism holds it (the caller has told it about a rack by the next frame).
        const float target = in.locked ? m_p.lockAt : 0.0f;
        const float span = std::max(m_springFrom - target, 0.0f);
        const float duration = m_p.springHome * span / m_p.travel;
        m_springT += dt;
        if (span <= 0.0f || m_springT >= duration) {
            m_springing = false;
            m_drawn = in.locked ? m_p.lockAt : -1.0f;
        } else {
            m_drawn = m_springFrom - span * (m_springT / duration);
        }
    } else {
        m_drawn = in.locked ? m_p.lockAt : -1.0f;
    }
    o.back = m_drawn;
    return o;
}

}
