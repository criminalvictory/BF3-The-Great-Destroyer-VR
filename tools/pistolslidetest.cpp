// The train pistol's slide worked by the left hand (src/pistolslide.cpp, as hands.cpp runs it), on scripted frames at
// 72 Hz, without the game. The pistol is held still in the view (the head's space: x right, y up, z back), turned off
// its axes (three poses: square, 25/-12/8 degrees and 170/30/-40, yaw/pitch/roll), and the left palm centre is put in
// the pistol's gun space (its frame bone's bind space: x the gun's left, y up, z to the muzzle) and carried into the
// view by that pose, unless a check holds it still in the view instead. pistolslide's numbers (its Profile): the hold
// on the slide's rear at (0.0123 0.0959 0.0551), grabbed within 7 cm by a fresh press of the left trigger (past 0.6
// after it was last below 0.4); 4.57 cm of travel; let go past 4.0 cm it racks; the slide stop at 4.1 cm, a tug of
// 4 mm off it; home in 70 ms from fully back; the trigger's rack back in 30 ms.
//   1  the overhand hold: pistolposes.h kSlidePalm is rigid and puts the bare palm centre (hands.cpp kLeftPalmCentre)
//      on the Profile's hold, within 0.5 mm
//   2  the reach: a press on the hold grabs (0 mm), 6.9 cm off grabs, 7.1 cm off doesn't (pressed, its distance told),
//      in each of three directions and three poses
//   3  fresh presses: a trigger held as the hand comes to the slide, or as the pistol comes into hand, grabs nothing;
//      let go and pressed again, it grabs; a trigger wavering 0.45-0.75 makes one press, and holding on through it
//      doesn't let go; a press with no palm or a broken one says so and grabs nothing; with no pistol nothing's told
//   4  the ride: the slide follows the hand back along the frame's -z exactly, stops at 4.57 cm with the hand going on,
//      doesn't move as the hand goes across or up, nor forward of home; the pistol and the hand moving together don't
//      move it; the pistol pushed forward under a hand still in the view pulls it as far
//   5  let go: fully back it racks and is home (the game's own drawn) 5 frames after the let go's (83 ms in all),
//      never going back on the way; 3.95 cm doesn't rack, 4.05 does; a 2 cm pull is home 2 frames after
//   6  caught springing home: pressed again a frame after the let go, it's held where it was
//   7  the slide stop: drawn there; grabbed there; never forward of it while held; a 3 mm tug goes back onto it, a
//      4.5 mm one racks and, the stop dropped, it springs home from it (63 ms), not in one jump
//   8  the trigger's rack (Release): from home back to 4.57 cm and home again within 10 frames; from the stop, home
//   9  lost: the hand lost while held lets go (racked if it was back), the pistol out of hand lets go (nothing drawn,
//      not racked); neither grabs again until the trigger's let go and pressed
//  10  frame times of 0, 1 s, -1 and not a number, and palms not a number: nothing drawn past its travel or not a
//      number
// Usage: pistolslidetest [-v]   (-v prints every check)
// The exit code is the number of failed checks.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "../src/pistolposes.h"
#include "../src/pistolslide.h"

namespace {

constexpr float kDt = 1.0f / 72.0f;
constexpr float kLeftPalmCentre[3] = {0.5182f, 1.1075f, 0.1756f};  // hands.cpp's: the bare left palm's vertex middle
const pistolslide::Profile kProfile;

bool g_verbose = false;
int g_checks = 0, g_failures = 0;

void Check(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (ok && !g_verbose)
        return;
    char text[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (!ok)
        ++g_failures;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", text);
}

// ---- Rigid poses (3x4, rows: rotation | translation) ----
void Compose(const float* a, const float* b, float* out) {
    float r[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        r[i * 4 + 3] += a[i * 4 + 3];
    }
    std::memcpy(out, r, sizeof(r));
}
void Transform(const float* m, const float* p, float* out) {
    float r[3];
    for (int i = 0; i < 3; ++i)
        r[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
    std::memcpy(out, r, sizeof(r));
}
void ToLocal(const float* m, const float* p, float* out) {  // the inverse of a rigid m on a point
    const float d[3] = {p[0] - m[3], p[1] - m[7], p[2] - m[11]};
    for (int i = 0; i < 3; ++i)
        out[i] = m[i] * d[0] + m[4 + i] * d[1] + m[8 + i] * d[2];
}
void Rotation(float yawDeg, float pitchDeg, float rollDeg, float* m) {  // about y, then x, then z
    const float k = 3.14159265f / 180.0f, cy = std::cos(yawDeg * k), sy = std::sin(yawDeg * k),
                cp = std::cos(pitchDeg * k), sp = std::sin(pitchDeg * k), cr = std::cos(rollDeg * k),
                sr = std::sin(rollDeg * k);
    const float Y[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy}, X[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp},
                Z[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
    float t[9], r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            t[i * 3 + j] = Y[i * 3] * X[j] + Y[i * 3 + 1] * X[3 + j] + Y[i * 3 + 2] * X[6 + j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = t[i * 3] * Z[j] + t[i * 3 + 1] * Z[3 + j] + t[i * 3 + 2] * Z[6 + j];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            m[i * 4 + j] = r[i * 3 + j];
        m[i * 4 + 3] = 0.0f;
    }
}
float Distance(const float* a, const float* b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// The pistol held in the view, the left hand, the slide, and what it said.
struct Rig {
    pistolslide::Slide slide;
    float pose[12] = {};
    float palm[3] = {};  // in the view
    bool hand = true, pistol = true, locked = false;
    float trigger = 0.0f;
    pistolslide::Output o;
    int presses = 0, grabs = 0, letGos = 0, racks = 0;
    float palmFrom = 0.0f, pulled = 0.0f;
    bool sane = true;  // the slide never drawn past its travel, nor as not a number

    explicit Rig(int which = 1) {
        const float turns[3][3] = {{0, 0, 0}, {25, -12, 8}, {170, 30, -40}};
        Rotation(turns[which][0], turns[which][1], turns[which][2], pose);
        pose[3] = 0.16f, pose[7] = -0.22f, pose[11] = -0.38f;
        Hold();
        Steps(3);
    }
    // The palm centre at a point of the gun's space.
    void PalmAt(const float* gun) {
        Transform(pose, gun, palm);
    }
    // ... on the hold on the slide's rear, that far back along the frame's -z and off it (the gun's x, y).
    void Hold(float back = 0.0f, float x = 0.0f, float y = 0.0f) {
        const float at[3] = {kProfile.grip[0] + x, kProfile.grip[1] + y, kProfile.grip[2] - back};
        PalmAt(at);
    }
    void Step(float dt = kDt) {
        pistolslide::Input in;
        in.dt = dt;
        in.pistolPose = pistol ? pose : nullptr;
        in.palm = hand ? palm : nullptr;
        in.trigger = trigger;
        in.locked = locked;
        o = slide.Update(in);
        presses += o.pressed, grabs += o.grabbed, letGos += o.letGo, racks += o.racked;
        if (o.pressed)
            palmFrom = o.palmFrom;
        if (o.letGo)
            pulled = o.pulled;
        if (!(o.back <= kProfile.travel + 1e-6f))  // (false for not a number too)
            sane = false;
        if (g_verbose)
            std::printf("      back %+.4f held %d%s%s%s\n", o.back, o.held, o.pressed ? " pressed" : "",
                        o.grabbed ? " grabbed" : "", o.letGo ? (o.racked ? " let go, racked" : " let go") : "");
    }
    void Steps(int n) {
        for (int i = 0; i < n; ++i)
            Step();
    }
    void Press() {
        trigger = 1.0f;
        Step();
    }
    void Release() {
        trigger = 0.0f;
        Step();
    }
    // Held: the hand drawn back from the hold to back over n frames.
    void PullTo(float back, int n = 12) {
        float from[3];
        ToLocal(pose, palm, from);
        for (int i = 1; i <= n; ++i) {
            const float at[3] = {from[0], from[1], from[2] + (kProfile.grip[2] - back - from[2]) * i / n};
            PalmAt(at);
            Step();
        }
    }
    // Frames until the slide is home (the game's own drawn), at most n; it never goes back on the way.
    int FramesHome(int n, bool& monotonic) {
        monotonic = true;
        float last = o.back;
        for (int i = 1; i <= n; ++i) {
            if (o.back < 0.0f)
                return i - 1;
            Step();
            if (o.back >= 0.0f && o.back > last + 1e-6f)
                monotonic = false;
            last = o.back;
        }
        return o.back < 0.0f ? n : n + 1;
    }
};

void OverhandHold() {
    std::printf("1 the overhand hold\n");
    const float* m = pistolposes::kSlidePalm;
    float most = 0.0f;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const float dot = m[i * 4] * m[j * 4] + m[i * 4 + 1] * m[j * 4 + 1] + m[i * 4 + 2] * m[j * 4 + 2];
            most = std::fmax(most, std::fabs(dot - (i == j ? 1.0f : 0.0f)));
        }
    const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                      m[2] * (m[4] * m[9] - m[5] * m[8]);
    Check(most < 1e-3f && std::fabs(det - 1.0f) < 1e-3f, "kSlidePalm is rigid (rows %.5f off orthonormal, det %.5f)", most,
          det);
    float palm[3];
    Transform(m, kLeftPalmCentre, palm);
    const float off = Distance(palm, kProfile.grip);
    Check(off < 0.0005f, "it puts the bare palm centre %.2f mm from the hold (%.4f %.4f %.4f)", off * 1000.0f, palm[0],
          palm[1], palm[2]);
}

void Reach() {
    std::printf("2 the reach\n");
    for (int which = 0; which < 3; ++which) {
        Rig r(which);
        r.Press();
        Check(r.o.grabbed && r.o.held && r.palmFrom < 1e-4f && r.o.back == 0.0f,
              "pose %d: a press on the hold grabs it (%.1f mm), held at home", which, r.palmFrom * 1000.0f);
        const float dirs[3][3] = {{1, 0, 0}, {0, 0.6f, 0.8f}, {-0.48f, -0.6f, -0.64f}};
        for (const auto& d : dirs) {
            for (float at : {0.069f, 0.071f}) {
                Rig s(which);
                s.Hold(-d[2] * at, d[0] * at, d[1] * at);
                s.Step();
                s.Press();
                const bool want = at < kProfile.reach;
                Check(s.presses == 1 && s.grabs == (want ? 1 : 0) && std::fabs(s.palmFrom - at) < 1e-4f,
                      "pose %d, %.1f cm off along (%.2f %.2f %.2f): pressed %d, %s (told %.1f mm)", which, at * 100, d[0],
                      d[1], d[2], s.presses, s.grabs ? "grabbed" : "not grabbed", s.palmFrom * 1000.0f);
            }
        }
    }
}

void FreshPresses() {
    std::printf("3 fresh presses\n");
    {
        Rig r;
        r.Hold(0.0f, 0.15f);
        r.Press();  // 15 cm off: pressed, not grabbed
        r.Hold();
        r.Steps(5);  // brought to the hold with it held
        Check(r.presses == 1 && r.grabs == 0 && !r.o.held, "a trigger held as the hand comes to the hold: nothing grabbed");
        r.Release();
        r.Press();
        Check(r.grabs == 1 && r.o.held, "let go and pressed again on the hold: grabbed");
    }
    {
        Rig r;
        r.pistol = false;
        r.Press();
        Check(r.presses == 0 && r.grabs == 0, "a press with no pistol in hand: nothing told, nothing grabbed");
        r.pistol = true;
        r.Steps(3);
        Check(r.grabs == 0 && !r.o.held, "the pistol into hand with the trigger held: nothing grabbed");
        r.Release();
        r.Press();
        Check(r.grabs == 1, "then let go and pressed: grabbed");
    }
    {
        Rig r;
        r.Hold(0.0f, 0.15f);
        r.Press();
        for (int i = 0; i < 144; ++i) {  // 2 s wavering about 0.6
            r.trigger = 0.6f + 0.15f * std::sin(i * 0.7f);
            r.Step();
        }
        Check(r.presses == 1, "a trigger wavering 0.45-0.75 after a press: %d press%s", r.presses,
              r.presses == 1 ? "" : "es");
        Rig h;
        h.Press();
        for (int i = 0; i < 144; ++i) {
            h.trigger = 0.6f + 0.15f * std::sin(i * 0.7f);
            h.Step();
        }
        Check(h.presses == 1 && h.letGos == 0 && h.o.held, "holding the slide through it: still held, not pressed again");
    }
    {
        Rig r;
        r.hand = false;
        r.Press();
        Check(r.presses == 1 && r.grabs == 0 && r.palmFrom < 0.0f, "a press with no palm: told (no palm), not grabbed");
        Rig n;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        n.palm[0] = nan;
        n.Press();
        Check(n.presses == 1 && n.grabs == 0 && n.palmFrom < 0.0f, "a press with a palm not a number: told, not grabbed");
    }
}

void Ride() {
    std::printf("4 the ride\n");
    Rig r;
    r.Press();
    float worst = 0.0f;
    for (int i = 1; i <= 60; ++i) {  // 1 mm a frame back to 6 cm
        r.Hold(0.001f * i);
        r.Step();
        worst = std::fmax(worst, std::fabs(r.o.back - std::fmin(0.001f * i, kProfile.travel)));
    }
    Check(r.o.held && worst < 1e-5f && std::fabs(r.o.back - kProfile.travel) < 1e-6f,
          "the slide follows the hand back (%.4f mm off at most) and stops at %.2f cm with the hand at 6", worst * 1000.0f,
          r.o.back * 100.0f);
    Rig a;
    a.Press();
    a.PullTo(0.02f);
    for (int i = 1; i <= 30; ++i) {
        a.Hold(0.02f, 0.001f * i, 0.0015f * i);
        a.Step();
    }
    Check(std::fabs(a.o.back - 0.02f) < 1e-5f, "the hand 3 cm across and 4.5 cm up at 2 cm back: still 2 cm back (%.2f)",
          a.o.back * 100.0f);
    a.PullTo(-0.03f);
    Check(a.o.back == 0.0f && a.o.held, "pushed 3 cm forward of home: at home (%.2f), held", a.o.back * 100.0f);
    // The pistol and the hand moving together (a swing, both hands): the slide doesn't move.
    Rig t;
    t.Press();
    float moved = 0.0f;
    for (int i = 1; i <= 72; ++i) {
        Rotation(25.0f + 0.5f * i, -12.0f + 0.3f * i, 8.0f, t.pose);
        t.pose[3] = 0.16f + 0.003f * i, t.pose[7] = -0.22f + 0.002f * i, t.pose[11] = -0.38f;
        t.Hold();
        t.Step();
        moved = std::fmax(moved, std::fabs(t.o.back));
    }
    Check(moved < 1e-5f, "the pistol and the hand swung together 36 degrees and 25 cm: the slide still home (%.4f mm)",
          moved * 1000.0f);
    // The pistol pushed forward under the hand, the hand still in the view.
    Rig f;
    f.Press();
    for (int i = 1; i <= 30; ++i) {
        for (int k = 0; k < 3; ++k)
            f.pose[k * 4 + 3] += f.pose[k * 4 + 2] * 0.001f;  // 1 mm a frame along the gun's own z (to the muzzle)
        f.Step();
    }
    Check(std::fabs(f.o.back - 0.03f) < 1e-5f, "the pistol pushed 3 cm forward under the hand: 3 cm back (%.3f)",
          f.o.back * 100.0f);
}

void LetGo() {
    std::printf("5 let go\n");
    Rig r;
    r.Press();
    r.PullTo(0.06f);
    r.Release();
    bool monotonic = true;
    const int frames = r.FramesHome(20, monotonic);
    Check(r.letGos == 1 && r.racks == 1 && std::fabs(r.pulled - kProfile.travel) < 1e-6f,
          "let go fully back: racked (pulled %.2f cm)", r.pulled * 100.0f);
    Check(frames <= 5 && monotonic, "home %d frames after the let go's (%.0f ms in all), never going back", frames,
          (frames + 1) * kDt * 1000.0f);
    for (float to : {0.0395f, 0.0405f}) {
        Rig s;
        s.Press();
        s.PullTo(to);
        s.Release();
        Check(s.racks == (to >= kProfile.rackAt ? 1 : 0), "let go %.2f cm back: %s", to * 100.0f,
              s.racks ? "racked" : "not racked");
    }
    Rig s;
    s.Press();
    s.PullTo(0.02f);
    s.Release();
    const int shortFrames = s.FramesHome(20, monotonic);
    Check(s.racks == 0 && shortFrames <= 2 && monotonic, "a 2 cm pull: not racked, home %d frames after", shortFrames);
}

void Caught() {
    std::printf("6 caught springing home\n");
    Rig r;
    r.Press();
    r.PullTo(kProfile.travel);
    r.Release();
    const float at = r.o.back;
    r.Hold(at);  // the hand on its rear where it's drawn
    r.Press();
    Check(r.grabs == 2 && r.o.held && std::fabs(r.o.back - at) < 1e-5f && r.palmFrom < 1e-4f,
          "pressed a frame after the let go on its rear: held where it was (%.2f cm)", r.o.back * 100.0f);
}

void SlideStop() {
    std::printf("7 the slide stop\n");
    Rig r;
    r.locked = true;
    r.Step();
    Check(std::fabs(r.o.back - kProfile.lockAt) < 1e-6f, "locked: drawn on the stop (%.2f cm)", r.o.back * 100.0f);
    r.Hold(kProfile.lockAt);
    r.Press();
    Check(r.grabs == 1 && r.palmFrom < 1e-4f && std::fabs(r.o.back - kProfile.lockAt) < 1e-6f,
          "grabbed on its rear there (%.1f mm from the hold)", r.palmFrom * 1000.0f);
    r.PullTo(kProfile.lockAt - 0.03f);
    Check(std::fabs(r.o.back - kProfile.lockAt) < 1e-6f, "pushed 3 cm forward: still on the stop (%.2f)",
          r.o.back * 100.0f);
    r.PullTo(kProfile.lockAt + 0.003f);
    r.Release();
    r.Steps(5);
    Check(r.racks == 0 && std::fabs(r.o.back - kProfile.lockAt) < 1e-6f, "a 3 mm tug and let go: back on the stop");
    r.Hold(kProfile.lockAt);
    r.Press();
    r.PullTo(kProfile.lockAt + 0.0045f);
    r.Release();
    Check(r.racks == 1, "a 4.5 mm tug and let go: racked");
    r.locked = false;  // the mechanism drops the stop
    bool monotonic = true;
    const int frames = r.FramesHome(20, monotonic);
    Check(frames >= 3 && frames <= 5 && monotonic, "the stop dropped: sprung home from it in %d frames, not in one jump",
          frames);
}

void TriggerRack() {
    std::printf("8 the trigger's rack\n");
    Rig r;
    r.slide.Release();
    float most = 0.0f;
    int frames = 0;
    for (; frames < 12; ++frames) {
        r.Step();
        most = std::fmax(most, r.o.back);
        if (r.o.back < 0.0f)
            break;
    }
    Check(std::fabs(most - kProfile.travel) < 1e-6f && frames <= 10,
          "from home: back to %.2f cm and home in %d frames", most * 100.0f, frames + 1);
    Rig s;
    s.locked = true;
    s.Step();
    s.slide.Release();
    s.locked = false;
    s.Step();
    bool monotonic = true;
    const int home = s.FramesHome(20, monotonic);
    Check(home <= 5 && monotonic, "from the stop, unlocked: home %d frames later", home + 1);
}

void Lost() {
    std::printf("9 lost\n");
    Rig r;
    r.Press();
    r.PullTo(0.045f);
    r.hand = false;
    r.Step();
    Check(r.letGos == 1 && r.racks == 1 && !r.o.held, "the hand lost 4.5 cm back: let go, racked");
    r.hand = true;
    r.Hold();
    r.Steps(10);
    Check(r.grabs == 1, "found again on the hold with the trigger still held: not grabbed again");
    Rig p;
    p.Press();
    p.PullTo(0.045f);
    p.pistol = false;
    p.Step();
    Check(p.letGos == 1 && p.racks == 0 && p.o.back < 0.0f, "the pistol out of hand: let go, not racked, nothing drawn");
    p.pistol = true;
    p.Hold();
    p.Steps(5);
    Check(p.grabs == 1 && !p.o.held && p.o.back < 0.0f, "back in hand with the trigger held: not grabbed, at home");
    p.Release();
    p.Press();
    Check(p.grabs == 2, "let go and pressed: grabbed");
}

void Degenerate() {
    std::printf("10 frame times and broken palms\n");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Rig r;
    r.Press();
    r.PullTo(0.03f);
    r.Release();
    for (float dt : {0.0f, 1.0f, -1.0f, nan})
        r.Step(dt);
    r.Steps(10);
    r.Hold();
    r.Press();
    r.palm[1] = nan;
    r.Step();
    r.Hold();
    r.Steps(3);
    r.Release();
    r.slide.Release();
    for (float dt : {nan, 1.0f, -1.0f, 0.0f})
        r.Step(dt);
    r.Steps(20);
    Check(r.sane && r.o.back < 0.0f, "never drawn past its travel or not a number; home at the end");
}

}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    std::printf("pistolslidetest: src/pistolslide.cpp at 72 Hz. The hold (%.4f %.4f %.4f) within %.0f cm, %.2f cm of "
                "travel, racked past %.1f, the stop at %.1f (a %.0f mm tug), home in %.0f ms.\n",
                kProfile.grip[0], kProfile.grip[1], kProfile.grip[2], kProfile.reach * 100, kProfile.travel * 100,
                kProfile.rackAt * 100, kProfile.lockAt * 100, kProfile.tug * 1000, kProfile.springHome * 1000);
    OverhandHold();
    Reach();
    FreshPresses();
    Ride();
    LetGo();
    Caught();
    SlideStop();
    TriggerRack();
    Lost();
    Degenerate();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
