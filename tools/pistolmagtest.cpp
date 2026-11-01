// The train pistol's magazine by the left hand (src/pistolmag.cpp, as hands.cpp runs it with build\pistol.txt "mag 1"),
// on scripted frames at 72 Hz, without the game: the design's reloadtest (screenshots\wf_reload\design) ported, its
// magazine's checks (the ammo and the slide come later), and the review's fixes since. The pistol is held still in the
// view (the head's space: x right, y up, z back), out front-right, a little down and canted (25/-12/8 degrees,
// yaw/pitch/roll), and the left hand is put where it holds a magazine at a place on the grip's line (pistolmag's
// GloveFor): its top s from home (0 seated, -11.08 cm at the grip's bottom, the well's mouth), off the line along the
// gun's x and turned about it. The geometry here is the design's own (geom.exe), not read from the module. The grip's
// own magazine is out (X) unless a check says otherwise; a seat puts it back in.
//   1  empty, a magazine from the pelvis: carried closed round it, the fingers open flat under the grip, caught, home
//   2  a squeeze at the pelvis that takes nothing: the magazine in (X first), the hand busy, no pistol
//   3  pushed 4 cm in and left there: it stays (parked); the palm pushes it home
//   4  the flat base needs no squeeze under the grip; moved away open, or let go while carried, it falls
//   5  what's caught and what isn't: turned 40 degrees, down from above, from below again; a one-frame jump
//   6  the game's own reload with a magazine in the hand: it's gone
//   7  the game's pinch as the carry: turned onto the palm under the grip, caught once it has
//   8  the snap (the review: the controllers can't overlap): 2.5 cm short isn't home, 1.5 is; two frames running; a
//      jump near home seats nothing; "seat 0.5"
//   9  each try's nearest approach (to set the snap from): parked, off the line, seated; the hands apart then
//  10  the push home (B2): its share over the last 3 cm; the hand drawn under the magazine on B, then on B2
//  11  the hand or the pistol lost: kept a second in the hand, parked in the well, waiting below the mouth
//  12  the carries and the helpers: C2 is the flat base's place, C1 53 degrees off; PlaceOnLine is the design's
//  13  frame times and poses not numbers: nothing drawn that isn't a number, never past home
// Usage: pistolmagtest [-v]   (-v prints every check and every event)
// The exit code is the number of failed checks.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include "../src/pistolmag.h"
#include "../src/pistolposes.h"

namespace {

constexpr float kDt = 1.0f / 72.0f;
constexpr float kPi = 3.14159265f;
// The design's geometry (geom.exe on the recordings): the seat (the magazine's bind space -> gun space home), the
// grip's line (the seat's y), the magazine's top in its bind space, and s at the grip's bottom.
const float kSeat[12] = {1, 0, 0, 0, 0, 0.96137f, -0.27524f, 0.13766f, 0, 0.27524f, 0.96137f, -0.02470f};
const float kLine[3] = {0.0f, 0.96137f, 0.27524f};
const float kTopBind[3] = {-0.0001f, -0.0926f, 0.1001f};
constexpr float kMouth = -0.1108f;
const float kLeftPalmCentre[3] = {0.5182f, 1.1075f, 0.1756f};  // hands.cpp's: the bare left palm's vertex middle

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
void Invert(const float* m, float* out) {
    float r[12];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        r[i * 4 + 3] = -(r[i * 4] * m[3] + r[i * 4 + 1] * m[7] + r[i * 4 + 2] * m[11]);
    std::memcpy(out, r, sizeof(r));
}
void Rotation(float yawDeg, float pitchDeg, float rollDeg, float* m) {  // about y, then x, then z (degrees)
    const float k = kPi / 180.0f, cy = std::cos(yawDeg * k), sy = std::sin(yawDeg * k), cp = std::cos(pitchDeg * k),
                sp = std::sin(pitchDeg * k), cr = std::cos(rollDeg * k), sr = std::sin(rollDeg * k);
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
float Angle(const float* m) {  // how far a pose turns (degrees)
    return std::acos(std::fmin(std::fmax((m[0] + m[5] + m[10] - 1.0f) * 0.5f, -1.0f), 1.0f)) * 180.0f / kPi;
}
bool Finite(const float* m, int n = 12) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(m[i]))
            return false;
    return true;
}
// The magazine at s along the line, off it along the gun's x and turned about the gun's x (through its top): gun
// space home -> gun space, from the design's geometry.
void OnLine(float s, float lateral, float turnDeg, float* inGun) {
    float r[12];
    Rotation(0, turnDeg, 0, r);
    float top[3], turned[3];
    Transform(kSeat, kTopBind, top);
    for (int i = 0; i < 3; ++i)
        turned[i] = r[i * 4] * top[0] + r[i * 4 + 1] * top[1] + r[i * 4 + 2] * top[2];
    const float off[3] = {lateral, 0, 0};
    std::memcpy(inGun, r, sizeof(r));
    for (int i = 0; i < 3; ++i)
        inGun[i * 4 + 3] = top[i] + s * kLine[i] + off[i] - turned[i];
}

// The pistol, the left hand and the grip, and what the module said.
struct Sim {
    float pistolPose[12] = {};
    float glove[12] = {};
    bool left = true, pistol = true, tracked = true, handFree = true, atPouch = false;
    bool wellEmpty = true;  // the pistol's own magazine out (X); a seat puts it back
    float squeeze = 0.0f, apart = 0.25f;
    pistolmag::Result r;
    std::vector<std::string> events;
    int frame = 0, seats = 0, tries = 0;
    pistolmag::Result lastTry;
    bool sane = true;  // every drawn pose a number; a magazine on the line never past home

    explicit Sim(float snap = 0.02f) {
        pistolmag::Clear();
        pistolmag::SetCarry(nullptr);
        pistolmag::SetSnap(snap);
        pistolmag::SetSeat(kSeat);
        Rotation(25, -12, 8, pistolPose);  // out front-right, a little down and canted
        pistolPose[3] = 0.16f, pistolPose[7] = -0.22f, pistolPose[11] = -0.38f;
        Step();  // the module sees the grip empty before anything's taken
    }
    void Note(const char* e) {
        events.push_back(e);
        if (g_verbose)
            std::printf("    [%4d] %s (s %+.3f)\n", frame, e, r.s);
    }
    bool Has(const char* e) const {
        for (const std::string& x : events)
            if (x.find(e) != std::string::npos)
                return true;
        return false;
    }
    int Count(const char* e) const {
        int n = 0;
        for (const std::string& x : events)
            n += x.find(e) != std::string::npos;
        return n;
    }
    void Step(bool reached = false, float dt = kDt) {
        ++frame;
        pistolmag::Frame f;
        f.dt = dt;
        f.pistol = pistol;
        f.pistolTracked = tracked;
        std::memcpy(f.pistolPose, pistolPose, sizeof(pistolPose));
        f.left = left;
        std::memcpy(f.glove, glove, sizeof(glove));
        f.leftGrip = {glove[3], glove[7], glove[11]};
        f.squeeze = squeeze;
        f.apart = apart;
        f.reached = reached;
        f.atPouch = atPouch;
        f.handFree = handFree;
        f.wellEmpty = wellEmpty;
        f.spare = true;
        r = pistolmag::Update(f);
        if (r.took)
            Note("took");
        if (reached && !r.took && *r.note)
            Note((std::string("no take: ") + r.note).c_str());
        if (r.flatOn)
            Note("flat on");
        if (r.flatOff)
            Note("flat off");
        if (r.caught)
            Note("caught");
        if (r.offLine)
            Note("off line");
        if (r.parked)
            Note("parked");
        if (r.pushed)
            Note("pushed");
        if (r.letGo)
            Note("let go");
        if (r.putBack)
            Note("put back");
        if (r.cancelled)
            Note("cancelled");
        if (r.seated) {
            Note("seated");
            wellEmpty = false;
            ++seats;
        }
        if (r.tried) {
            char text[96];
            std::snprintf(text, sizeof(text), "tried: nearest %+.1f cm, %.0f cm apart", r.closest * 100,
                          r.apartThen * 100);
            Note(text);
            lastTry = r;
            ++tries;
        }
        if ((r.mag && !Finite(r.magPose)) || (r.gloveBlend > 0.0f && !Finite(r.glove)) || !std::isfinite(r.s) ||
            !std::isfinite(r.flat) || !std::isfinite(r.push))
            sane = false;
        if (r.mag && (r.gloveBlend > 0.0f || pistolmag::Parked()) && r.s > 1e-6f)  // on the line or parked: past home
            sane = false;
    }
    void Steps(int n) {
        for (int i = 0; i < n; ++i)
            Step();
    }
    // The left hand holding the magazine at s along the line (lateral: off it along the gun's x; turn: degrees about
    // the gun's x), with the flat base's hold or the carry's.
    void MagAt(float s, float lateral = 0.0f, float turnDeg = 0.0f, bool flat = true) {
        float inGun[12], magPose[12];
        OnLine(s, lateral, turnDeg, inGun);
        Compose(pistolPose, inGun, magPose);
        pistolmag::GloveFor(magPose, flat, glove);
    }
    // The magazine carried along the line from s0 to s1 at speed (m/s).
    void Carry(float s0, float s1, float speed, float lateral = 0.0f, float turn = 0.0f) {
        const int n = std::max(1, static_cast<int>(std::fabs(s1 - s0) / speed / kDt));
        for (int i = 1; i <= n; ++i) {
            MagAt(s0 + (s1 - s0) * i / n, lateral, turn);
            Step();
        }
    }
    // A magazine taken at the pelvis and brought under the grip, flat, caught at 3 cm below its bottom.
    void TakeAndCatch() {
        MagAt(-0.45f);
        squeeze = 1.0f;
        Step(true);
        Carry(-0.45f, -0.14f, 0.6f);
        Steps(12);
        Carry(-0.14f, -0.13f, 0.2f);
    }
    // Where the hand drawn under the magazine puts the bare palm's middle, in gun space.
    void DrawnPalm(float* out) const {
        float inverse[12], inGun[12];
        Invert(pistolPose, inverse);
        Compose(inverse, r.glove, inGun);
        Transform(inGun, kLeftPalmCentre, out);
    }
};

void Empty() {
    std::printf("1 empty: a magazine from the pelvis into the grip\n");
    Sim m;
    m.MagAt(-0.45f, 0.02f, 20.0f);  // the hand at the pelvis
    m.squeeze = 1.0f;
    m.Step(true);
    Check(m.Has("took") && m.r.mag && m.r.holding, "the squeeze at the pelvis takes a magazine");
    Check(m.r.flat < 0.01f, "carried with the closed hand (flat %.2f)", m.r.flat);
    m.Carry(-0.45f, -0.135f, 0.8f);
    Check(m.Has("flat on"), "the hand goes flat under the grip (the top %.1f cm below it)", (kMouth - m.r.s) * 100);
    m.Steps(12);
    Check(m.r.flat > 0.99f, "the flat base eased in (%.2f)", m.r.flat);
    m.Carry(-0.135f, 0.01f, 0.4f);
    m.Steps(3);
    Check(m.Has("caught") && m.Has("seated") && !m.wellEmpty, "caught in the funnel and pushed home");
    Check(!pistolmag::Holding() && !m.r.mag && !m.r.holding, "nothing in the hand after: the pistol's own is drawn");
    Check(m.tries == 1 && m.lastTry.seated && m.lastTry.closest >= -0.02f,
          "one try at the grip, seated, the palm %.1f cm from home at the nearest", -m.lastTry.closest * 100);
    m.Steps(4);
    Check(m.r.gloveBlend == 0.0f, "the hand back where the controller has it 4 frames after");
    Check(m.sane, "everything drawn a number, nothing past home");
}

void Refused() {
    std::printf("2 a squeeze at the pelvis that takes nothing\n");
    Sim m;
    m.wellEmpty = false;
    m.Step();
    m.MagAt(-0.45f);
    m.squeeze = 1.0f;
    m.Step(true);
    Check(m.Has("X first") && !pistolmag::Holding(), "the magazine in: nothing taken (X first)");
    Sim b;
    b.handFree = false;
    b.MagAt(-0.45f);
    b.squeeze = 1.0f;
    b.Step(true);
    Check(b.Has("busy") && !pistolmag::Holding(), "the hand on the pistol or its slide: nothing taken (busy)");
    Sim p;
    p.pistol = false;
    p.MagAt(-0.45f);
    p.squeeze = 1.0f;
    p.Step(true);
    Check(p.Has("not in hand") && !pistolmag::Holding(), "no pistol in hand: nothing taken");
    Sim n;
    n.MagAt(-0.45f);
    n.squeeze = 1.0f;
    n.Step();
    Check(!pistolmag::Holding() && n.events.empty(), "a squeeze that didn't reach the pelvis: nothing, nothing said");
}

void ParkAndPush() {
    std::printf("3 parked in the grip, pushed home\n");
    Sim m;
    m.MagAt(-0.45f);
    m.squeeze = 1.0f;
    m.Step(true);
    m.Carry(-0.45f, -0.07f, 0.5f);
    m.Steps(2);
    Check(m.r.s > -0.075f && m.r.s < -0.065f, "4 cm into the grip (s %.3f)", m.r.s);
    m.squeeze = 0.0f;  // the hand opens ...
    m.Steps(3);
    Check(pistolmag::Holding() && !m.Has("let go"), "let go of in the grip: it stays (no fall)");
    for (int i = 0; i < 20; ++i) {  // ... and drops 10 cm
        m.MagAt(-0.07f - 0.005f * i);
        m.Step();
    }
    Check(pistolmag::Parked() && m.Has("parked") && std::fabs(m.r.s + 0.07f) < 0.005f, "parked where it was (s %.3f)",
          m.r.s);
    Check(!m.r.holding && m.r.mag, "the hand is free while it's parked; the magazine drawn in the grip");
    m.Carry(-0.17f, 0.01f, 1.2f);  // the open palm comes up under it and pushes it home
    m.Steps(3);
    Check(m.Has("pushed") && m.Has("seated") && !m.wellEmpty, "pushed home by the open palm");
    Check(!m.Has("let go"), "never fell");
    Check(m.sane, "everything drawn a number, nothing past home");
}

void FlatAndDrops() {
    std::printf("4 the flat base and the drops\n");
    Sim m;
    m.MagAt(-0.45f);
    m.squeeze = 1.0f;
    m.Step(true);
    m.Carry(-0.45f, -0.16f, 0.6f);
    m.Steps(12);
    Check(m.r.flat > 0.99f, "flat under the grip");
    m.squeeze = 0.0f;
    m.Steps(5);
    Check(pistolmag::Holding() && !m.Has("let go"), "the grip let go on the flat base: it stays on the palm");
    m.Carry(-0.16f, -0.45f, 1.0f);
    Check(m.Has("let go") && !pistolmag::Holding(), "moved away with the hand open: it falls");
    Check(m.wellEmpty && m.seats == 0, "nothing went in");
    Sim t;
    t.MagAt(-0.45f);
    t.squeeze = 1.0f;
    t.Step(true);
    for (int i = 0; i < 6; ++i) {
        t.MagAt(-0.45f, 0.01f * i);
        t.Step();
    }
    t.squeeze = 0.0f;
    t.Step();
    const float* v = t.r.dropVelocity;
    const float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    Check(t.Has("let go") && speed > 0.3f, "let go while carried: it falls at the hand's speed (%.2f m/s)", speed);
    Sim a;
    a.MagAt(-0.45f);
    a.squeeze = 1.0f;
    a.Step(true);
    a.Steps(3);
    a.atPouch = true;
    a.squeeze = 0.0f;
    a.Step();
    Check(a.Has("put back") && !a.Has("let go") && !pistolmag::Holding(), "let go at the pelvis: put back, no fall");
}

void Catches() {
    std::printf("5 what's caught and what isn't\n");
    Sim s;
    s.MagAt(-0.45f);
    s.squeeze = 1.0f;
    s.Step(true);
    s.Carry(-0.45f, -0.115f, 0.5f, 0.0f, 40.0f);
    s.Steps(3);
    Check(!s.Has("caught"), "turned 40 degrees in the funnel: not caught");
    Sim a;
    a.MagAt(-0.45f, 0.08f);
    a.squeeze = 1.0f;
    a.Step(true);
    a.Carry(-0.45f, -0.05f, 0.6f, 0.08f);  // up beside the grip
    a.Carry(-0.05f, -0.05f, 0.6f, 0.0f);   // across into it (the top above the mouth)
    for (int i = 0; i < 8; ++i) {
        a.MagAt(-0.05f - 0.01f * i, 0.08f * (1.0f - i / 8.0f));
        a.Step();
    }
    a.Carry(-0.13f, -0.12f, 0.2f);  // down into the funnel from above
    Check(!a.Has("caught"), "down into the funnel from above: not caught");
    a.Carry(-0.12f, -0.20f, 0.6f);  // below the funnel, then up again
    a.Carry(-0.20f, -0.10f, 0.4f);
    Check(a.Has("caught"), "from below again: caught");
    Sim j;
    j.MagAt(-0.45f);
    j.squeeze = 1.0f;
    j.Step(true);
    j.Carry(-0.45f, -0.08f, 0.6f);
    const float before = j.r.s;
    j.MagAt(0.2f);  // one frame 28 cm up
    j.Step();
    j.MagAt(-0.08f);
    j.Steps(4);
    Check(j.r.s - before <= 0.0301f && !j.Has("seated"), "a one-frame jump moved it %.1f cm, not home",
          (j.r.s - before) * 100);
}

void GameReload() {
    std::printf("6 the game's own reload\n");
    Sim s;
    s.MagAt(-0.45f);
    s.squeeze = 1.0f;
    s.Step(true);
    s.Carry(-0.45f, -0.3f, 0.6f);
    s.wellEmpty = false;  // X held: the game's reload puts its own in
    s.Step();
    Check(s.Has("cancelled") && !pistolmag::Holding() && !s.Has("let go"), "the hand's magazine gone, no fall");
}

void GamePinchCarry() {
    std::printf("7 the game's pinch as the carry\n");
    Sim s;
    pistolmag::SetCarry(pistolposes::kOnPalmGamePinch);
    s.MagAt(-0.45f, 0, 0, false);  // carried with the pinch, lined up
    s.squeeze = 1.0f;
    s.Step(true);
    for (int i = 1; i <= 30; ++i) {
        s.MagAt(-0.45f + (0.31f * i) / 30, 0, 0, false);
        s.Step();
    }
    Check(s.Has("flat on"), "under the grip the flat base comes on");
    Check(!s.Has("caught"), "not caught before it's turned onto the palm");
    for (int i = 0; i < 15; ++i) {  // the hand turned to keep it lined up as it goes onto the palm
        s.MagAt(-0.14f, 0, 0, true);
        s.Step();
    }
    s.Carry(-0.14f, 0.01f, 0.4f);
    s.Steps(3);
    Check(s.Has("caught") && s.Has("seated"), "then caught and pushed home");
    pistolmag::SetCarry(nullptr);
}

void Snap() {
    std::printf("8 the snap\n");
    Sim a;
    a.TakeAndCatch();
    Check(a.Has("caught"), "caught under the grip");
    a.Carry(-0.13f, -0.025f, 0.4f);
    a.Steps(30);
    Check(!a.Has("seated") && pistolmag::Holding() && std::fabs(a.r.s + 0.025f) < 0.002f,
          "held 2.5 cm short with the snap at 2: it stays there on the line (s %.3f)", a.r.s);
    a.MagAt(-0.015f);
    a.Step();
    const int at = a.frame;
    while (!a.Has("seated") && a.frame < at + 10)
        a.Step();
    Check(a.Has("seated") && a.frame - at <= 2, "brought to 1.5 cm short: home by itself, seated %d frames on",
          a.frame - at);
    Sim one;
    one.TakeAndCatch();
    one.Carry(-0.13f, -0.05f, 0.4f);
    one.Steps(3);
    one.MagAt(-0.01f);  // a single frame within the snap (the palm's push), then back
    one.Step();
    one.MagAt(-0.05f);
    one.Steps(10);
    Check(!one.Has("seated"), "within it for one frame only: not home (s %.3f)", one.r.s);
    Sim j;
    j.TakeAndCatch();
    j.Carry(-0.13f, -0.04f, 0.4f);
    j.Steps(3);
    j.MagAt(0.2f);  // a tracking jump: one frame 24 cm up
    j.Step();
    j.MagAt(-0.04f);
    j.Steps(30);
    Check(!j.Has("seated") && pistolmag::Holding() && j.r.s <= -0.0099f,
          "a one-frame jump from 4 cm short: 3 cm on at most (held up by the grip, s %.3f), not home", j.r.s);
    j.Carry(-0.04f, -0.015f, 0.3f);
    j.Steps(4);
    Check(j.Has("seated"), "then pushed within the snap: home");
    Sim f(0.005f);
    f.TakeAndCatch();
    f.Carry(-0.13f, -0.015f, 0.4f);
    f.Steps(30);
    Check(!f.Has("seated") && std::fabs(f.r.s + 0.015f) < 0.002f, "seat 0.5: 1.5 cm short isn't near enough");
    f.Carry(-0.015f, -0.003f, 0.2f);
    f.Steps(4);
    Check(f.Has("seated"), "seat 0.5: 3 mm short is");
    Sim w(0.5f);  // a snap wider than the grip: kept to 10 cm
    w.TakeAndCatch();
    w.Carry(-0.13f, -0.115f, 0.2f);  // just below the mouth
    w.Steps(10);
    Check(!w.Has("seated"), "seat 50: kept to 10 cm (below the grip's bottom nothing snaps)");
    w.Carry(-0.115f, -0.095f, 0.2f);
    w.Steps(6);
    Check(w.Has("seated"), "seat 50: 9.5 cm short, a centimetre into the grip, snaps");
    Check(a.sane && j.sane && f.sane && w.sane, "everything drawn a number, nothing past home");
}

void Tries() {
    std::printf("9 each try's nearest approach\n");
    Sim p;
    p.TakeAndCatch();
    for (int i = 1; i <= 20; ++i) {  // up to 6 cm short, the hands coming together as it goes
        const float s = -0.13f + (0.07f * i) / 20;
        p.apart = 0.30f + s;
        p.MagAt(s);
        p.Step();
    }
    p.Steps(5);
    for (int i = 0; i < 20; ++i) {  // the hand drops away: parked
        p.MagAt(-0.06f - 0.005f * i);
        p.Step();
    }
    Check(p.Has("parked") && p.tries == 1 && std::fabs(p.lastTry.closest + 0.06f) < 0.001f &&
              std::fabs(p.lastTry.apartThen - 0.24f) < 0.001f,
          "parked: the try's nearest %.1f cm short (6), the hands %.1f cm apart then (24)", -p.lastTry.closest * 100,
          p.lastTry.apartThen * 100);
    Check(p.lastTry.parked, "told in the same frame as the park");
    Sim o;
    o.TakeAndCatch();
    o.Carry(-0.13f, -0.125f, 0.2f);
    o.Carry(-0.125f, -0.125f, 0.2f, 0.06f);  // 6 cm aside below the grip: off the line
    o.Steps(4);
    Check(o.Has("off line") && o.tries == 1 && o.lastTry.offLine && o.lastTry.closest > -0.126f &&
              o.lastTry.closest < -0.12f,
          "pulled off the line below the grip: a try told, nearest %.1f cm short", -o.lastTry.closest * 100);
    Sim s;
    s.TakeAndCatch();
    s.Carry(-0.13f, -0.012f, 0.4f);
    s.Steps(4);
    Check(s.tries == 1 && s.lastTry.seated && s.lastTry.closest > -0.0125f && s.lastTry.closest < -0.0115f,
          "seated by the snap: its try told, nearest %.1f cm short (1.2)", -s.lastTry.closest * 100);
    Sim g;
    g.TakeAndCatch();
    g.Carry(-0.13f, -0.08f, 0.4f);
    g.wellEmpty = false;
    g.Step();
    Check(g.Has("cancelled") && g.tries == 1, "gone (the game's reload): its try told too");
}

void PushHome() {
    std::printf("10 the push home (B2)\n");
    float onB[3], onB2[3], inverse[12];
    Invert(pistolposes::kOnPalmFlat, inverse);
    Transform(inverse, kLeftPalmCentre, onB);  // the palm's middle with the magazine home, on B
    Invert(pistolposes::kOnPalmSeated, inverse);
    Transform(inverse, kLeftPalmCentre, onB2);
    Sim m(0.005f);
    m.TakeAndCatch();
    m.Carry(-0.13f, -0.05f, 0.4f);
    m.Steps(2);
    float palm[3], want[3];
    m.DrawnPalm(palm);
    for (int i = 0; i < 3; ++i)
        want[i] = onB[i] + m.r.s * kLine[i];
    Check(m.r.push == 0.0f && m.r.flat == 1.0f && m.r.gloveBlend == 1.0f && Distance(palm, want) < 0.001f,
          "5 cm short: the flat base (push %.2f), the hand drawn on B (the palm %.2f mm off)", m.r.push,
          Distance(palm, want) * 1000);
    m.Carry(-0.05f, -0.015f, 0.2f);
    m.Steps(2);
    Check(std::fabs(m.r.push - 0.5f) < 0.02f, "1.5 cm short: half the push home (%.2f)", m.r.push);
    m.Carry(-0.015f, 0.0f, 0.2f);
    while (!m.Has("seated") && m.frame < 2000)
        m.Step();
    m.DrawnPalm(palm);  // the seat's frame: the hand still drawn under it at home
    Check(m.Has("seated") && m.r.seated && Distance(palm, onB2) < 0.001f,
          "home: the hand drawn on B2, the palm under the floorplate (%.2f mm off; (%.4f %.4f %.4f))",
          Distance(palm, onB2) * 1000, palm[0], palm[1], palm[2]);
    const float geocheck[3] = {0.0093f, -0.1107f, 0.0038f};  // review\geocheck.txt: B2 as recorded
    Check(Distance(onB2, geocheck) < 0.0005f, "B2's palm as the review measured it (%.2f mm off)",
          Distance(onB2, geocheck) * 1000);
    m.Steps(1);
    Check(m.r.gloveBlend > 0.0f && m.r.gloveBlend < 1.0f, "after the seat the hand eases back (%.2f)", m.r.gloveBlend);
}

void Lost() {
    std::printf("11 the hand or the pistol lost\n");
    Sim h;
    h.MagAt(-0.45f);
    h.squeeze = 1.0f;
    h.Step(true);
    h.Steps(3);
    h.left = false;
    for (int i = 0; i < 36; ++i)  // half a second
        h.Step();
    Check(pistolmag::Holding() && !h.r.mag && !h.Has("cancelled"), "the hand lost half a second: kept, not drawn");
    h.left = true;
    h.Step();
    Check(h.r.mag && h.r.holding, "found again: in the hand");
    h.left = false;
    for (int i = 0; i < 80; ++i)
        h.Step();
    Check(h.Has("cancelled") && !pistolmag::Holding() && !h.Has("let go"), "lost over a second: gone, no fall");
    Sim w;
    w.TakeAndCatch();
    w.Carry(-0.13f, -0.06f, 0.4f);
    w.left = false;
    w.Step();
    Check(w.Has("parked") && pistolmag::Parked() && w.r.mag, "lost on the line in the grip: parked there at once");
    Sim b;
    b.TakeAndCatch();
    b.left = false;
    b.Steps(30);
    Check(pistolmag::Holding() && !pistolmag::Parked() && b.r.mag && b.r.holding,
          "lost on the line below the grip: it waits there");
    b.left = true;
    b.Carry(-0.13f, -0.01f, 0.4f);
    b.Steps(4);
    Check(b.Has("seated"), "found again: pushed home");
    Sim p;
    p.MagAt(-0.45f);
    p.squeeze = 1.0f;
    p.Step(true);
    p.pistol = false;
    for (int i = 0; i < 36; ++i)
        p.Step();
    Check(pistolmag::Holding() && !p.Has("cancelled"), "the pistol out of hand half a second: kept");
    p.pistol = true;
    p.Step();
    p.pistol = false;
    for (int i = 0; i < 80; ++i)
        p.Step();
    Check(p.Has("cancelled") && !pistolmag::Holding(), "out of hand over a second: gone");
    Sim t;
    t.MagAt(-0.45f);
    t.squeeze = 1.0f;
    t.Step(true);
    t.tracked = false;
    t.squeeze = 0.0f;
    t.Step();
    Check(t.Has("cancelled") && !t.Has("let go"), "let go while the pistol isn't tracked: gone, no fall");
}

void Carries() {
    std::printf("12 the carries and the helpers\n");
    float a[12], b[12], inverse[12], d[12];
    const float home[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    pistolmag::SetCarry(nullptr);
    pistolmag::GloveFor(home, false, a);
    pistolmag::GloveFor(home, true, b);
    float most = 0.0f;
    for (int i = 0; i < 12; ++i)
        most = std::fmax(most, std::fabs(a[i] - b[i]));
    Check(most < 1e-6f, "C2: the carry holds it where the flat base does (%.1e)", most);
    pistolmag::SetCarry(pistolposes::kOnPalmGamePinch);
    pistolmag::GloveFor(home, false, a);
    Invert(a, inverse);
    Compose(inverse, b, d);
    Check(std::fabs(Angle(d) - 53.4f) < 0.5f, "C1: 53 degrees off the flat base (%.1f)", Angle(d));
    pistolmag::SetCarry(nullptr);
    for (const float* hold : {pistolposes::kOnPalmFlat, pistolposes::kOnPalmSeated, pistolposes::kOnPalmGamePinch}) {
        float worst = 0.0f;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const float dot =
                    hold[i * 4] * hold[j * 4] + hold[i * 4 + 1] * hold[j * 4 + 1] + hold[i * 4 + 2] * hold[j * 4 + 2];
                worst = std::fmax(worst, std::fabs(dot - (i == j ? 1.0f : 0.0f)));
            }
        Check(worst < 1e-4f, "a hold on the palm is rigid (rows %.1e off orthonormal)", worst);
    }
    float mine[12], theirs[12];
    for (float s : {0.0f, -0.05f, -0.30f})
        for (float turn : {0.0f, 35.0f}) {
            OnLine(s, 0.0f, turn, mine);
            pistolmag::PlaceOnLine(s, turn, theirs);
            float off = 0.0f;
            for (int i = 0; i < 12; ++i)
                off = std::fmax(off, std::fabs(mine[i] - theirs[i]));
            Check(off < 1e-5f, "PlaceOnLine(%.2f, %.0f) is the design's place (%.1e)", s, turn, off);
        }
}

void Degenerate() {
    std::printf("13 frame times and poses not numbers\n");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Sim m;
    m.MagAt(-0.45f);
    m.squeeze = 1.0f;
    m.Step(true);
    for (float dt : {0.0f, 1.0f, -1.0f, nan})
        m.Step(false, dt);
    m.Carry(-0.45f, -0.13f, 0.6f);
    m.Steps(12);
    m.glove[5] = nan;  // a broken pose: as the hand not seen
    m.Step();
    m.apart = nan;
    m.Carry(-0.13f, -0.06f, 0.4f);
    m.pistolPose[7] = nan;  // the pistol's pose broken a frame
    m.Step();
    m.pistolPose[7] = -0.22f;
    m.Carry(-0.06f, -0.01f, 0.4f);
    for (float dt : {nan, 1.0f, -1.0f, 0.0f})
        m.Step(false, dt);
    m.Steps(6);
    Check(m.sane, "nothing drawn that isn't a number, never past home");
    Check(m.Has("seated"), "and it went home in the end");
}

}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    std::printf("pistolmagtest: src/pistolmag.cpp at 72 Hz. The grip's line 16 degrees back, its bottom %.2f cm below "
                "home; the snap 2 cm unless said.\n",
                -kMouth * 100);
    Empty();
    Refused();
    ParkAndPush();
    FlatAndDrops();
    Catches();
    GameReload();
    GamePinchCarry();
    Snap();
    Tries();
    PushHome();
    Lost();
    Carries();
    Degenerate();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
