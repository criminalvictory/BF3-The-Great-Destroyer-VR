#pragma once

namespace pistolslide {

// The train pistol's slide (The Great Destroyer's M9) worked by the left hand (docs\research\pistol-manual-reload.md).
// Its own, not reload.cpp's charging handle (which stays the M416's):
//   - grabbed overhand at its rear (the palm on its top, the fingers over its right side's serrations, the thumb on
//     its left: pistolposes.h) by a fresh press of the left trigger (the user's choice, as the M416's handle), the
//     palm within 7 cm of that hold. A press is the trigger past 0.6 after it was last below 0.4, so a trigger
//     resting near 0.6 isn't pressed again and again;
//   - pulled back along the frame's -z as far as the hand goes, at most its full travel (4.57 cm, the game's own
//     shots), and let go: past 4.0 cm it feeds (racked: the caller tells the mechanism); from the slide stop (locked
//     open on an empty magazine) a tug 4 mm further back drops the stop;
//   - it springs home in 70 ms from fully back (the game's shot cycle comes forward in about 80), or onto the stop;
//   - while the mechanism holds it open (locked) it's drawn there, not where the game's animation has it (the game
//     still has its hidden round, so its own slide goes home after the last shot);
//   - held, the pistol is out of battery: the trigger doesn't fire (input.cpp).
// Pure: no game calls and no logs (Output says what happened), so tools\pistolslidetest.cpp runs it on scripted
// paths. Gun space: the pistol frame bone's (3) bind space (x the gun's left, y up, z to the muzzle; metres). View
// space: the head's (hands.cpp's).

struct Profile {
    float travel = 0.0457f;    // m, fully back (the recordings: z -0.2996 home, -0.3457 back)
    float lockAt = 0.0410f;    // m, held open by the slide stop (to set by eye: the game never shows it)
    float rackAt = 0.0400f;    // m, pulled at least this far and let go: it feeds
    float tug = 0.004f;        // m, from the stop this much further and let go: the stop drops
    float springHome = 0.07f;  // s, from fully back to home
    // The palm centre (the bare left palm bone's vertex middle: hands.cpp kLeftPalmCentre) with the hand overhand on
    // the slide's rear, the slide closed, in gun space (posefit.exe; pistolposes.h kSlidePalm puts the palm there):
    // the grab is measured from here, moved back with the slide.
    float grip[3] = {0.0123f, 0.0959f, 0.0551f};
    float reach = 0.07f;  // m
};

struct Input {
    float dt = 0.0f;
    const float* pistolPose = nullptr;  // gun space -> view (nullptr: no pistol in hand, or the slide isn't worked)
    const float* palm = nullptr;        // the left palm centre in the view (nullptr: not tracked, or busy)
    float trigger = 0.0f;               // the left controller's, 0-1
    bool locked = false;                // the mechanism holds the slide open (on the slide stop)
};

struct Output {
    float back = -1.0f;  // the slide drawn this far back (m); below 0: as the game's own animation has it
    bool held = false;   // the left hand has it (drawn on it, sliding with it; the trigger doesn't fire)
    // Events this frame.
    bool pressed = false;   // a fresh press of the left trigger with the pistol in hand, grabbed or not
    float palmFrom = 0.0f;  // ... the palm this far from the hold (m; below 0: no palm, not tracked or busy)
    bool grabbed = false, letGo = false;
    bool racked = false;   // let go past rackAt, or tugged off the stop: it feeds (the mechanism's Rack)
    float pulled = 0.0f;   // let go: the most this grab pulled it (m, from home)
};

class Slide {
public:
    explicit Slide(const Profile& p = Profile()) : m_p(p) {}
    Output Update(const Input& in);
    // The trigger racked it (the mechanism's automatic rack): from the stop it springs home; from home (an empty
    // chamber with the slide forward) it cycles as a shot does, back in 30 ms, then home.
    void Release() {
        m_cycleBack = m_drawn < 0.0f;
        m_springFrom = m_cycleBack ? m_p.travel : m_drawn;
        m_springT = 0.0f;
        m_springing = true;
    }
    bool Held() const { return m_held; }
    Profile& Settings() { return m_p; }

private:
    Profile m_p;
    bool m_held = false;
    bool m_armed = true;  // the trigger below 0.4 since the last press: past 0.6 is a press
    bool m_startLocked = false;
    float m_grabZ = 0.0f, m_start = 0.0f, m_pull = 0.0f, m_maxPull = 0.0f;
    bool m_springing = false, m_cycleBack = false;
    float m_springFrom = 0.0f, m_springT = 0.0f;
    float m_drawn = -1.0f;
};

}
