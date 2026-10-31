// The left hand steering the gun while it holds the handguard (src/steer.h, as hands.cpp runs it), on
// scripted frames at 72 Hz, without the game. The scripts are in the room (OpenXR LOCAL: x right, y up,
// z back, metres): the right hand's grip at the origin and the gun's line from it to the hold point
// 30 cm ahead (3 cm up: the handguard sits above the grip) as the right hand alone points it ("from"),
// the line from the right hand to the left ("to") and how far apart the hands are, the hold taken or let
// go as hands.cpp Support gives them. The gun's line is Output.turn applied to "from", held against the
// hands' line or the right hand's own. (hands.cpp now gives steer.h both lines in the gun's own model
// space, where a swing of both hands or a recentre changes neither; steer.h's maths is the same in any
// frame, and the room's scripts are the harder test of it.)
// steer.h's numbers: the left hand steers fully while its line is within 35 degrees of the right
// hand's, not at all past 65, and fully from 15 cm apart, not at all within 10; the weight eased with
// a 0.04 s time constant (29% of the way a frame at 72 Hz); the steer itself turns at most 500 degrees
// a second (6.9 a frame). The tolerances below come from those.
//   1  a hold steered 20 degrees by the left hand, taken at the start, both hands swinging the gun
//      together for 3 s (yaw +-60 and pitch +-40 degrees, each at up to 120 deg/s): the gun on the
//      hands' line within 0.5 deg from 0.2 s on
//   2  the left hand pivots the muzzle 30 degrees, the right hand still, (a) in 0.1 s, (b) in 0.05 s
//      (faster than the steer may turn): on the hands' line within 0.5 deg from 0.15 s after the pivot
//      starts, never past it (0.1 deg)
//   3  the left hand wanders, still holding, off the handguard to beside the right hand (a), below it
//      (b), below and to the left (c), and on behind it (the hands' line 0 to 180 degrees off the right
//      hand's in 2 s, 30 cm apart): within 1 deg of the right hand's own line from 0.12 s (three of
//      the ease's time constants) after the angle passes 65 degrees, on it (0.01 deg) at the end; no
//      frame turning the gun 6 degrees or more; never NaN
//   4  the hands come within 10 cm while swinging right at 120 deg/s (30 to 8 cm apart in 0.3 s, the
//      hands' line 10 to 30 degrees off the right hand's): no frame turning the gun more than 6 degrees
//      against the right hand; on the right hand's line (0.05 deg) 0.25 s later
//   5  let go with the steer at 30 degrees, (a) the hands still, (b) turning right at 90 deg/s: within
//      0.5 deg of the right hand's line from 0.2 s after, on it (0.01 deg) from 0.3 s; no frame turning
//      the gun more than 10 degrees against the right hand; its angle off that line never growing
//   6e the left controller lost for 10 frames while holding a 30-degree steer (hands.cpp coasts): the
//      gun stays where it was (0.01 deg), and nothing moves once it's found again where it was
//   6  a one-frame glitch of the left controller in a hold steered 15 degrees, the hands' line jumping
//      (a) 40 degrees across (25 the other side: steered fully), (b) 40 degrees further out (55: partly
//      steered), (c) 150 degrees (out of the cone), then back; (d) the left controller lost for the
//      frame (no line) in a hold steered 30 degrees at half weight (the hands 12.5 cm apart): no frame
//      turning the gun 8 degrees or more; within 1 deg of where it was 0.3 s after; (d) never further
//      off the right hand's line than before (0.1 deg)
//   7  the line back while the last steer still eases out: (a) re-grabbed 1 to 14 frames after letting
//      go of a hold steered 40 degrees (the new hold where the hand is, so the hands' line is the right
//      hand's own: hands.cpp takes the hold point from the palm), (b) the hands apart again after 1 to
//      14 frames within 10 cm of each other in a hold steered 24 degrees, the hands' line now 10
//      degrees the other way: the gun never tilting towards the old steer more than 2 deg beyond the
//      same let-go's (the hands staying together's) own ease-out
//   8  degenerate inputs (from == to, from == -to, to zero, to or from NaN, dt 0, 0.5, -1 or NaN, apart
//      0 or NaN), fresh and mid-hold, three frames each and then three ordinary ones: (a) the turns and
//      the weights finite, the turns unit quaternions, the weights within 0-1, the gun finite; (b)
//      Output.angle finite (or -1: no line)
//   9  an Update takes under 2 us
// Usage: steertest [-v ID]   (-v prints that check frame by frame, e.g. -v 7a)
// The exit code is the number of failed checks.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "../src/steer.h"

namespace {

constexpr double kPi = 3.14159265358979;
constexpr double kDt = 1.0 / 72.0;  // a frame at 72 Hz (13.9 ms)
constexpr float kTogether = 0.10f;  // hands.cpp kHandsTogether: closer, the right hand alone points the gun
constexpr double kHoldAt = 0.30;    // metres from the right hand's grip to the left hand on the handguard

std::string g_verbose;  // -v: the check printed frame by frame

double Deg(double radians) {
    return radians * 180 / kPi;
}
double Rad(double degrees) {
    return degrees * kPi / 180;
}
int Frames(double seconds) {
    return static_cast<int>(std::lround(seconds / kDt));
}
// 0 to 1, eased in and out, as a hand moves.
double Ease(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * (3 - 2 * u);
}

// ---- Directions and turns in double (steer.h itself works in float) ----
struct V3 {
    double x = 0, y = 0, z = 0;
    V3() = default;
    V3(double a, double b, double c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};
double Dot(const V3& a, const V3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 Cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double Len(const V3& a) {
    return std::sqrt(Dot(a, a));
}
V3 Unit(const V3& a) {
    const double l = Len(a);
    return l > 1e-15 ? a * (1.0 / l) : a;
}
bool Finite(const V3& a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
// The angle between two directions (degrees, 0 to 180), from the cross and the dot: it keeps its
// digits near 0 and 180, where the dot's arc cosine loses them (the checks go down to 0.01 deg).
double AngleDeg(const V3& a, const V3& b) {
    return Deg(std::atan2(Len(Cross(a, b)), Dot(a, b)));
}
// The signed angle from a to b about axis (degrees), for a and b at right angles to it.
double SignedDeg(const V3& a, const V3& b, const V3& axis) {
    return Deg(std::atan2(Dot(Cross(a, b), axis), Dot(a, b)));
}

struct Q {
    double x = 0, y = 0, z = 0, w = 1;
};
Q AxisAngle(const V3& axis, double radians) {
    const V3 a = Unit(axis);
    const double s = std::sin(radians / 2);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians / 2)};
}
Q Mul(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Q Conj(const Q& q) {
    return {-q.x, -q.y, -q.z, q.w};
}
// v turned by unit q: v + w t + u x t, with t = 2 u x v (u the vector part).
V3 Rotate(const Q& q, const V3& v) {
    const V3 u(q.x, q.y, q.z);
    const V3 t = Cross(u, v) * 2.0;
    return v + t * q.w + Cross(u, t);
}
// How far a turn turns (degrees, 0 to 180).
double TurnDeg(const Q& q) {
    return Deg(2 * std::atan2(std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), std::fabs(q.w)));
}
// The smallest turn taking unit a onto unit b (not opposite ones).
Q Arc(const V3& a, const V3& b) {
    const V3 c = Cross(a, b);
    const Q q{c.x, c.y, c.z, 1 + Dot(a, b)};
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-12 ? Q{q.x / n, q.y / n, q.z / n, q.w / n} : Q{};
}
Q FromSteer(const steer::Quat& q) {
    return {q.x, q.y, q.z, q.w};
}

// ---- The hands ----
// The gun's line from the right hand's grip to where the left holds it, in the right hand's own axes
// (the room's while the hand is level and faces ahead): 30 cm ahead and 3 cm up.
const V3 kLine = Unit(V3(0.0, 0.03, -0.2985));

// A direction at right angles to the gun's line (the right hand's axes), from a rough one: the axis
// the hands' line is turned about off the right hand's.
V3 Across(const V3& rough) {
    return Unit(rough - kLine * Dot(rough, kLine));
}
// The left hand off the right hand's grip (the right hand's axes): the gun's line turned degrees
// about axis, metres out.
V3 LeftAt(const V3& axis, double degrees, double metres = kHoldAt) {
    return Rotate(AxisAngle(axis, Rad(degrees)), kLine) * metres;
}

// One run of the steer, a frame at a time, and what the gun did.
struct Sim {
    std::string id;
    bool verbose = false;
    steer::State state;
    steer::Output out;
    V3 from, to, gun;                 // this frame's in the room: the right hand's own line, the hands', the gun's
    double offHands = 0, offOwn = 0;  // the gun's angle off the hands' line and off the right hand's (degrees)
    double moved = 0, movedHand = 0;  // its turn since the last frame: in the room, and against the right hand
    bool finite = true;               // every frame's turn, weight and gun so far
    int frames = 0;
    Q handBefore;
    V3 gunBefore;

    explicit Sim(const char* name = "") : id(name), verbose(!g_verbose.empty() && g_verbose == name) {}

    // A frame as hands.cpp gives it: the right hand's turn in the room (its grip at the origin), the left
    // hand off it (room axes, metres), whether the left hand holds the handguard. Not holding, the input
    // stays as hands.cpp resets it every frame (no line, only the frame's time).
    void Step(const Q& rightHand, const V3& left, bool holding, double dt = kDt) {
        const V3 own = Rotate(rightHand, kLine);
        const double apart = Len(left);
        const V3 line = apart > 0 ? left * (1 / apart) : V3();
        steer::Input in;
        in.dt = static_cast<float>(dt);
        if (holding) {
            in.holding = true;
            in.apart = static_cast<float>(apart);
            in.together = kTogether;
            in.lineOk = apart > 0.001;  // and the hold 30 cm from the grip (hands.cpp wants 5 cm at least)
            for (int i = 0; i < 3; ++i)
                in.from[i] = static_cast<float>(own[i]), in.to[i] = static_cast<float>(line[i]);
        }
        Run(in, rightHand, line);
    }

    // A frame from a given input; the gun is the turn applied to the right hand's own line.
    void Run(const steer::Input& in, const Q& rightHand, const V3& hands) {
        out = steer::Update(in, state);
        from = Rotate(rightHand, kLine);
        to = hands;
        gun = Rotate(FromSteer(out.turn), from);
        finite = finite && Finite(gun) && std::isfinite(out.turn.x) && std::isfinite(out.turn.y) &&
                 std::isfinite(out.turn.z) && std::isfinite(out.turn.w) && std::isfinite(out.weight);
        offHands = AngleDeg(gun, to);
        offOwn = AngleDeg(gun, from);
        moved = movedHand = 0;
        if (frames > 0) {
            moved = AngleDeg(gun, gunBefore);
            movedHand = AngleDeg(Rotate(Conj(rightHand), gun), Rotate(Conj(handBefore), gunBefore));
        }
        gunBefore = gun, handBefore = rightHand;
        ++frames;
        if (verbose)
            std::printf("    %-3s %4d %6.3f s %-7s apart %5.3f angle %6.2f wanted %.3f weight %.3f steer %6.2f | off the "
                        "hands' line %6.2f, the right hand's %6.2f; turned %5.2f (against the right hand %5.2f)\n",
                        id.c_str(), frames, frames * kDt, in.holding ? "holding" : "-", in.apart,
                        out.angle >= 0 ? Deg(out.angle) : -1.0, out.wanted, out.weight,
                        TurnDeg(FromSteer(state.steer)), offHands, offOwn, moved, movedHand);
    }
};

// ---- Reporting ----
int g_failed = 0, g_counted = 0;

void Report(const char* id, const char* what, bool pass, const char* format, ...) {
    char numbers[1536];
    va_list args;
    va_start(args, format);
    std::vsnprintf(numbers, sizeof(numbers), format, args);
    va_end(args);
    std::printf("%-3s %-5s %s | %s\n", id, pass ? "PASS" : "FAIL", what, numbers);
    ++g_counted;
    g_failed += !pass;
}

// ---- 1. Both hands swinging the gun together ----
// The steer is the turn from where the right hand points the gun onto where the hands do; swinging both
// together turns it in the room only as far as its axis swings with them (2 sin 10 degrees of the hands'
// turn for a 20 degree steer): the rate limit never holds it back, and once eased in the gun is on the
// hands' line whatever the swing.
void Check1() {
    Sim sim("1");
    const V3 left = LeftAt(Across(V3(0.3, 1.0, 0.0)), 20);  // the hands' line off to the left, a little down
    double worst = 0, first = -1, weightFirst = 0, rate = 0;
    Q targetBefore;
    for (int i = 0; i <= Frames(3.0); ++i) {
        const double t = i * kDt;
        const Q hand = Mul(AxisAngle(V3(0, 1, 0), Rad(60 * std::sin(2 * t))),
                           AxisAngle(V3(1, 0, 0), Rad(40 * std::sin(3 * t))));
        sim.Step(hand, Rotate(hand, left), true);
        // What the steer had to follow: the turn from the right hand's line onto the hands'.
        const Q target = Arc(sim.from, sim.to);
        if (i > 0)
            rate = std::max(rate, TurnDeg(Mul(target, Conj(targetBefore))) / kDt);
        targetBefore = target;
        if (t >= 0.2 - 1e-9) {
            if (first < 0)
                first = sim.offHands, weightFirst = sim.out.weight;
            worst = std::max(worst, sim.offHands);
        }
    }
    Report("1", "both hands swinging the gun together, steered 20 deg", worst <= 0.5 && sim.finite,
           "the gun %.3f deg off the hands' line at most from 0.2 s (<= 0.5; %.3f then, the weight %.4f); the steer "
           "had to turn %.0f deg/s at most (it may 500)%s",
           worst, first, weightFirst, rate, sim.finite ? "" : "; NOT FINITE");
}

// ---- 2. The left hand pivoting the muzzle, the right hand still ----
void Check2() {
    struct Case {
        const char* id;
        const char* what;
        double seconds;
    };
    const Case cases[] = {
        {"2a", "the left hand pivots the muzzle 30 deg in 0.1 s", 0.1},
        {"2b", "the left hand pivots the muzzle 30 deg in 0.05 s (faster than the steer may turn)", 0.05},
    };
    const V3 axis = Across(V3(0, 1, 0));  // the muzzle swung to the left
    for (const Case& c : cases) {
        Sim sim(c.id);
        for (int i = 0; i < Frames(0.5); ++i)  // held on the right hand's own line: the weight full
            sim.Step(Q{}, LeftAt(axis, 0), true);
        double worst = 0, past = -1e9, lastOff = -1;
        for (int i = 1; i <= Frames(0.5); ++i) {
            const double t = i * kDt;
            sim.Step(Q{}, LeftAt(axis, 30 * Ease(t / c.seconds)), true);
            past = std::max(past, SignedDeg(sim.from, sim.gun, axis) - 30);
            if (t >= 0.15 - 1e-9)
                worst = std::max(worst, sim.offHands);
            if (sim.offHands > 0.5)
                lastOff = t;
        }
        char settled[96];
        if (lastOff < 0)
            std::snprintf(settled, sizeof(settled), "never more than 0.5 deg off it");
        else
            std::snprintf(settled, sizeof(settled), "more than 0.5 deg off it last at %.3f s", lastOff);
        Report(c.id, c.what, worst <= 0.5 && past <= 0.1 && sim.finite,
               "%.3f deg off the hands' line at most from 0.15 s (<= 0.5), %s; %+.3f deg past it at most (<= 0.1)",
               worst, settled, std::max(past, 0.0));
    }
}

// ---- 3. The left hand wandering off the handguard, still holding ----
// The hold lasts until the grip is let go (the user, 2026-10-30), so the left hand may wander anywhere
// while it holds: past 65 degrees the gun must be the right hand's alone, after the ease.
void Check3() {
    struct Case {
        const char* id;
        const char* what;
        V3 rough;     // the axis the hands' line turns about
        double sign;  // which way about it
    };
    const Case cases[] = {
        {"3a", "the left hand wandering, still holding, out to beside the right hand and on behind it", V3(0, 1, 0), 1},
        {"3b", "the left hand wandering, still holding, down below the right hand and on behind it", V3(1, 0, 0), -1},
        {"3c", "the left hand wandering, still holding, down to the left of the right hand and on behind it",
         V3(-1, 1, 0), 1},
    };
    constexpr double kRate = 90;  // degrees a second: 0 to 180 in 2 s
    const double passed65 = 65 / kRate, settleFrom = passed65 + 3 * steer::kEase;
    for (const Case& c : cases) {
        const V3 axis = Across(c.rough);
        Sim sim(c.id);
        for (int i = 0; i < Frames(0.3); ++i)
            sim.Step(Q{}, LeftAt(axis, 0), true);
        double after = 0, most = 0, mostMoved = 0, at65 = -1;
        for (int i = 1; i <= Frames(2.0); ++i) {
            const double t = i * kDt, angle = std::min(kRate * t, 180.0);
            sim.Step(Q{}, LeftAt(axis, c.sign * angle), true);
            most = std::max(most, sim.offOwn);
            mostMoved = std::max(mostMoved, sim.moved);
            if (at65 < 0 && angle >= 65)
                at65 = sim.offOwn;
            if (t >= settleFrom - 1e-9)
                after = std::max(after, sim.offOwn);
        }
        const bool pass = after <= 1 && sim.offOwn <= 0.01 && mostMoved < 6 && sim.finite;
        Report(c.id, c.what, pass,
               "%.3f deg off the right hand's line at most from %.3f s (65 deg + 3 time constants; <= 1), %.4f at the end "
               "(<= 0.01); %.2f off as the hands' line passed 65 deg; turned %.2f deg in a frame at most (< 6); followed "
               "the left hand %.1f deg at most%s",
               after, settleFrom, sim.offOwn, at65, mostMoved, most, sim.finite ? "" : "; NOT FINITE");
    }
}

// ---- 4. The hands coming within 10 cm, swinging far right ----
// The left arm can't follow the right across the body: the left hand comes back towards the right one
// and off the gun's line, where its line is least steady.
void Check4() {
    Sim sim("4");
    const V3 axis = Across(V3(0.5, 1.0, 0.0));  // the left hand coming in from the left and below
    const V3 p0 = LeftAt(axis, 10, 0.30), p1 = LeftAt(axis, 30, 0.08);
    for (int i = 0; i < Frames(0.3); ++i)
        sim.Step(Q{}, p0, true);
    double most = 0, mostRoom = 0, mostOff = 0, at15 = -1, at10 = -1;
    for (int i = 1; i <= Frames(0.55); ++i) {
        const double t = i * kDt;
        const Q hand = AxisAngle(V3(0, 1, 0), Rad(-120 * t));  // swinging right
        const V3 rel = p0 + (p1 - p0) * std::min(t / 0.3, 1.0);
        sim.Step(hand, Rotate(hand, rel), true);
        most = std::max(most, sim.movedHand);
        mostRoom = std::max(mostRoom, sim.moved);
        mostOff = std::max(mostOff, sim.offOwn);
        if (at15 < 0 && Len(rel) < 0.15)
            at15 = t;
        if (at10 < 0 && Len(rel) < 0.10)
            at10 = t;
    }
    Report("4", "the hands within 10 cm, swinging right at 120 deg/s (30 to 8 cm in 0.3 s)",
           most <= 6 && sim.offOwn <= 0.05 && sim.finite,
           "turned %.2f deg in a frame at most against the right hand (<= 6; %.2f in the room, the swing's own %.2f); "
           "%.4f deg off the right hand's line at the end (<= 0.05); the hands 15 cm apart at %.3f s, 10 cm at %.3f s; "
           "followed the left hand %.1f deg at most%s",
           most, mostRoom, 120 * kDt, sim.offOwn, at15, at10, mostOff, sim.finite ? "" : "; NOT FINITE");
}

// ---- 5. Letting go with the steer at 30 degrees ----
void Check5() {
    struct Case {
        const char* id;
        const char* what;
        double rate;  // degrees a second the hands turn right
    };
    const Case cases[] = {{"5a", "let go with the steer at 30 deg, the hands still", 0},
                          {"5b", "let go with the steer at 30 deg, the hands turning right at 90 deg/s", 90}};
    const V3 left = LeftAt(Across(V3(0, 1, 0)), 30);
    for (const Case& c : cases) {
        Sim sim(c.id);
        const auto hand = [&c](int n) { return AxisAngle(V3(0, 1, 0), Rad(-c.rate * n * kDt)); };
        int frame = 0;
        for (; frame < Frames(0.5); ++frame)
            sim.Step(hand(frame), Rotate(hand(frame), left), true);
        const double held = sim.offOwn;
        double before = held, grew = 0, mostMoved = 0, from02 = 0, from03 = 0;
        for (int i = 1; i <= Frames(0.35); ++i, ++frame) {
            sim.Step(hand(frame), Rotate(hand(frame), left), false);
            mostMoved = std::max(mostMoved, sim.movedHand);
            grew = std::max(grew, sim.offOwn - before);
            before = sim.offOwn;
            if (i * kDt >= 0.2 - 1e-9)
                from02 = std::max(from02, sim.offOwn);
            if (i * kDt >= 0.3 - 1e-9)
                from03 = std::max(from03, sim.offOwn);
        }
        Report(c.id, c.what, from02 <= 0.5 && from03 <= 0.01 && mostMoved <= 10 && grew <= 1e-3 && sim.finite,
               "held %.2f deg off the right hand's line; %.3f at most from 0.2 s after (<= 0.5), %.4f from 0.3 s (<= "
               "0.01); turned %.2f deg in a frame at most against the right hand (<= 10); grew %.4f deg at most (none)%s",
               held, from02, from03, mostMoved, grew, sim.finite ? "" : "; NOT FINITE");
    }
}

// ---- 6. A one-frame glitch of the left controller ----
// Inside-out tracking loses the left controller's place now and then, most of all with the hands close.
void Check6() {
    struct Case {
        const char* id;
        const char* what;
        double glitch;  // degrees off the right hand's line for the one frame (the hold at 15)
    };
    const Case cases[] = {
        {"6a", "a one-frame glitch, the hands' line 40 deg across (25 the other side: steered fully)", -25},
        {"6b", "a one-frame glitch, the hands' line 40 deg further out (55: partly steered)", 55},
        {"6c", "a one-frame glitch, the hands' line 150 deg off (out of the cone)", -135},
    };
    const V3 axis = Across(V3(0, 1, 0));
    for (const Case& c : cases) {
        Sim sim(c.id);
        for (int i = 0; i < Frames(0.5); ++i)
            sim.Step(Q{}, LeftAt(axis, 15), true);
        const V3 before = sim.gun;
        sim.Step(Q{}, LeftAt(axis, c.glitch), true);
        double most = sim.moved;
        for (int i = 1; i <= Frames(0.3); ++i) {
            sim.Step(Q{}, LeftAt(axis, 15), true);
            most = std::max(most, sim.moved);
        }
        const double back = AngleDeg(sim.gun, before);
        Report(c.id, c.what, most < 8 && back <= 1 && sim.finite,
               "turned %.2f deg in a frame at most (< 8); %.4f deg from where it was 0.3 s after (<= 1)%s", most, back,
               sim.finite ? "" : "; NOT FINITE");
    }
    // (d) The left controller lost for a frame: hands.cpp gives no line then (Support isn't run), and the
    // steer's weight dips. Coming back with the same hands, the gun goes back to where it was; with the
    // steer only half on (the hands 12.5 cm apart) a fix that put the weight back to full and re-turned
    // the steer from the gun would carry it past there first.
    {
        Sim sim("6d");
        const V3 left = LeftAt(axis, 30, 0.125);
        for (int i = 0; i < Frames(0.5); ++i)
            sim.Step(Q{}, left, true);
        const V3 before = sim.gun;
        const double off = sim.offOwn;
        sim.Step(Q{}, left, false);
        double most = sim.moved, past = -1e9;
        for (int i = 1; i <= Frames(0.3); ++i) {
            sim.Step(Q{}, left, true);
            most = std::max(most, sim.moved);
            past = std::max(past, sim.offOwn - off);
        }
        const double back = AngleDeg(sim.gun, before);
        Report("6d", "the left controller lost for a frame, the steer half on (30 deg, the hands 12.5 cm apart)",
               most < 8 && back <= 1 && past <= 0.1 && sim.finite,
               "held %.2f deg off the right hand's line; turned %.2f deg in a frame at most (< 8); %.4f deg from where it "
               "was 0.3 s after (<= 1); %+.3f deg further off than before at most (<= 0.1)%s",
               off, most, back, std::max(past, 0.0), sim.finite ? "" : "; NOT FINITE");
    }
    // (e) The left controller lost for 10 frames while holding (0.14 s, under hands.cpp's 0.2 s): hands.cpp
    // coasts (Input.coast) and the gun stays as the hands last had it; found again where it was, nothing
    // moves. (hands.cpp gives both lines in the gun's own model space, so the right hand turning on
    // meanwhile changes neither: the hand stays unturned here.)
    {
        Sim sim("6e");
        const V3 left = LeftAt(axis, 30);
        for (int i = 0; i < Frames(0.5); ++i)
            sim.Step(Q{}, left, true);
        const double off = sim.offOwn;
        double drift = 0, most = 0;
        for (int i = 0; i < 10; ++i) {
            steer::Input in;
            in.dt = static_cast<float>(kDt);
            in.holding = in.coast = true;
            sim.Run(in, Q{}, left * (1 / kHoldAt));
            drift = std::max(drift, std::fabs(sim.offOwn - off));
        }
        for (int i = 0; i < Frames(0.3); ++i) {
            sim.Step(Q{}, left, true);
            most = std::max(most, sim.movedHand);
        }
        Report("6e", "the left controller lost for 10 frames while holding (coasting), the steer at 30 deg",
               drift <= 0.01 && most <= 0.01 && sim.finite,
               "held %.2f deg off the right hand's line; %.4f deg from that while lost (<= 0.01); turned %.4f deg against the "
               "right hand at most once found again (<= 0.01)%s",
               off, drift, most, sim.finite ? "" : "; NOT FINITE");
    }
}

// ---- 7. The line back while the last steer still eases out ----
// Letting go, the steer stays and only its weight eases out; while that weight is 0.01 or more (13
// frames) a new line doesn't start afresh: the steer is turned from the old one at 500 deg/s while the
// weight eases back in, and the gun must not swing back towards the old steer meanwhile.
void Check7() {
    const V3 axis = Across(V3(0, 1, 0));
    // (a) A hold steered 40 degrees let go, then a fresh one k frames later where the hand is.
    {
        double worst = -1e9, weightAt = 0, offAt = 0, peak = 0, twinAtPeak = 0;
        int worstK = 0;
        std::string byK;
        for (int k = 1; k <= 14; ++k) {
            Sim sim("7a"), twin;
            sim.verbose = false;
            for (int i = 0; i < Frames(0.5); ++i) {
                sim.Step(Q{}, LeftAt(axis, 40), true);
                twin.Step(Q{}, LeftAt(axis, 40), true);
            }
            sim.verbose = g_verbose == "7a";
            if (sim.verbose)
                std::printf("    7a: re-grabbed %d frames after letting go\n", k);
            for (int i = 0; i < k; ++i) {
                sim.Step(Q{}, LeftAt(axis, 40), false);
                twin.Step(Q{}, LeftAt(axis, 40), false);
            }
            const double weight = sim.state.weight, off = sim.offOwn;
            double excess = -1e9, peakK = 0, twinK = 0;
            for (int i = 0; i < Frames(0.3); ++i) {
                sim.Step(Q{}, LeftAt(axis, 0), true);  // the new hold where the hand is
                twin.Step(Q{}, LeftAt(axis, 40), false);
                const double e = sim.offOwn - twin.offOwn;
                if (e > excess)
                    excess = e, peakK = sim.offOwn, twinK = twin.offOwn;
            }
            char one[32];
            std::snprintf(one, sizeof(one), " %d:%.1f", k, excess);
            byK += one;
            if (excess > worst)
                worst = excess, worstK = k, weightAt = weight, offAt = off, peak = peakK, twinAtPeak = twinK;
        }
        Report("7a", "re-grabbed 1-14 frames after letting go of a hold steered 40 deg", worst <= 2,
               "worst re-grabbed %d frames after (the weight %.3f, the gun %.2f deg off the right hand's line): then %.2f "
               "deg off where the ease-out alone is %.2f, %.2f beyond it (<= 2); beyond it by frames after:%s",
               worstK, weightAt, offAt, peak, twinAtPeak, worst, byK.c_str());
    }
    // (b) A hold steered 24 degrees to the left, the hands within 10 cm for m frames (the steer stays,
    // its weight easing out), then apart again with the hands' line 10 degrees to the right.
    {
        double worst = -1e9, weightAt = 0, offAt = 0, peak = 0, twinAtPeak = 0;
        int worstM = 0;
        std::string byM;
        for (int m = 1; m <= 14; ++m) {
            Sim sim("7b"), twin;
            sim.verbose = false;
            for (int i = 0; i < Frames(0.5); ++i) {
                sim.Step(Q{}, LeftAt(axis, 24), true);
                twin.Step(Q{}, LeftAt(axis, 24), true);
            }
            sim.verbose = g_verbose == "7b";
            if (sim.verbose)
                std::printf("    7b: apart again after %d frames together\n", m);
            for (int i = 0; i < m; ++i) {
                sim.Step(Q{}, LeftAt(axis, 24, 0.08), true);
                twin.Step(Q{}, LeftAt(axis, 24, 0.08), true);
            }
            const double weight = sim.state.weight, off = SignedDeg(sim.from, sim.gun, axis);
            double excess = -1e9, peakM = 0, twinM = 0;
            for (int i = 0; i < Frames(0.3); ++i) {
                sim.Step(Q{}, LeftAt(axis, -10), true);
                twin.Step(Q{}, LeftAt(axis, 24, 0.08), true);
                // Positive: to the left, towards the old steer.
                const double tilt = SignedDeg(sim.from, sim.gun, axis), twinTilt = SignedDeg(twin.from, twin.gun, axis);
                if (tilt - twinTilt > excess)
                    excess = tilt - twinTilt, peakM = tilt, twinM = twinTilt;
            }
            char one[32];
            std::snprintf(one, sizeof(one), " %d:%.1f", m, excess);
            byM += one;
            if (excess > worst)
                worst = excess, worstM = m, weightAt = weight, offAt = off, peak = peakM, twinAtPeak = twinM;
        }
        Report("7b", "the hands apart again after 1-14 frames within 10 cm, the hands' line now 10 deg the other way",
               worst <= 2,
               "worst after %d frames together (the weight %.3f, the gun %+.2f deg towards the old steer): then %+.2f deg "
               "where the ease-out alone is %+.2f, %.2f beyond it (<= 2); beyond it by frames together:%s",
               worstM, weightAt, offAt, peak, twinAtPeak, worst, byM.c_str());
    }
}

// ---- 8. Degenerate inputs ----
bool UnitQuat(const steer::Quat& q) {
    const double n = std::sqrt(static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
                               static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w);
    return std::isfinite(n) && std::fabs(n - 1) < 1e-4;
}
bool InUnit(float v) {  // finite and within 0-1
    return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
}

void Check8() {
    steer::Input ordinary;  // a hold steered 20 degrees
    ordinary.holding = true, ordinary.lineOk = true, ordinary.apart = 0.30f, ordinary.together = kTogether;
    ordinary.dt = static_cast<float>(kDt);
    const V3 hands = Unit(LeftAt(Across(V3(0, 1, 0)), 20));
    for (int i = 0; i < 3; ++i)
        ordinary.from[i] = static_cast<float>(kLine[i]), ordinary.to[i] = static_cast<float>(hands[i]);
    struct Case {
        const char* what;
        void (*make)(steer::Input&);
    };
    const Case cases[] = {
        {"from == to", [](steer::Input& in) { std::memcpy(in.to, in.from, sizeof(in.to)); }},
        {"from == -to", [](steer::Input& in) { for (int i = 0; i < 3; ++i) in.to[i] = -in.from[i]; }},
        {"to zero", [](steer::Input& in) { std::memset(in.to, 0, sizeof(in.to)); }},
        {"to NaN", [](steer::Input& in) { for (float& v : in.to) v = std::numeric_limits<float>::quiet_NaN(); }},
        {"from NaN", [](steer::Input& in) { for (float& v : in.from) v = std::numeric_limits<float>::quiet_NaN(); }},
        {"dt 0", [](steer::Input& in) { in.dt = 0.0f; }},
        {"dt 0.5", [](steer::Input& in) { in.dt = 0.5f; }},
        {"dt -1", [](steer::Input& in) { in.dt = -1.0f; }},
        {"dt NaN", [](steer::Input& in) { in.dt = std::numeric_limits<float>::quiet_NaN(); }},
        {"apart 0", [](steer::Input& in) { in.apart = 0.0f; }},
        {"apart NaN", [](steer::Input& in) { in.apart = std::numeric_limits<float>::quiet_NaN(); }},
    };
    int runs = 0, bad = 0, badAngle = 0;
    std::string badWhere, badAngleWhere;
    for (const Case& c : cases) {
        for (int midHold = 0; midHold < 2; ++midHold) {
            steer::State st;
            if (midHold)
                for (int i = 0; i < Frames(0.5); ++i)
                    steer::Update(ordinary, st);
            steer::Input odd = ordinary;
            c.make(odd);
            bool ok = true, okAngle = true;
            for (int i = 0; i < 6; ++i) {
                const steer::Output o = steer::Update(i < 3 ? odd : ordinary, st);
                ok = ok && UnitQuat(o.turn) && InUnit(o.weight) && InUnit(o.wanted) && UnitQuat(st.steer) &&
                     InUnit(st.weight) && Finite(Rotate(FromSteer(o.turn), kLine));
                okAngle = okAngle && (o.angle == -1.0f || (std::isfinite(o.angle) && o.angle >= 0.0f && o.angle <= 3.1416f));
            }
            ++runs;
            const std::string where = std::string(" ") + c.what + (midHold ? " (mid-hold)" : " (fresh)");
            if (!ok)
                ++bad, badWhere += where;
            if (!okAngle)
                ++badAngle, badAngleWhere += where;
        }
    }
    // Not counted: what a frame without a usable dt does (hands.cpp gives 0 only on its very first frame
    // and caps the rest at 0.1 s): the weight goes all the way to the wanted at once, the steer stays put.
    steer::State fresh;
    steer::Input zero = ordinary;
    zero.dt = 0.0f;
    const float atOnce = steer::Update(zero, fresh).weight;
    Report("8a", "degenerate inputs: the turns, the weights and the gun", bad == 0,
           "%d of %d runs not finite, not unit or out of 0-1%s%s (a dt of 0 takes a fresh hold's weight to %.2f in its "
           "first frame)",
           bad, runs, bad ? ":" : "", badWhere.c_str(), atOnce);
    Report("8b", "degenerate inputs: Output.angle finite or -1 (no line)", badAngle == 0,
           "%d of %d runs gave another angle (NaN)%s%s", badAngle, runs, badAngle ? ":" : "", badAngleWhere.c_str());
}

// ---- 9. The time an Update takes ----
void Check9() {
    // The hands' line wandering in and out of the cone, near and far, held and not: every path is timed
    // (the fresh start, the rate limit's slerp, the ease out without a line).
    steer::Input in[64];
    const V3 axis = Across(V3(0.4, 1.0, 0.0));
    for (int k = 0; k < 64; ++k) {
        const V3 hands = Unit(LeftAt(axis, (k * 37) % 100));
        in[k].holding = k % 13 != 0;
        in[k].lineOk = true;
        in[k].apart = 0.05f + 0.01f * static_cast<float>(k % 30);
        in[k].together = kTogether;
        in[k].dt = static_cast<float>(kDt);
        for (int i = 0; i < 3; ++i)
            in[k].from[i] = static_cast<float>(kLine[i]), in[k].to[i] = static_cast<float>(hands[i]);
    }
    LARGE_INTEGER frequency, a, b;
    QueryPerformanceFrequency(&frequency);
    steer::State st;
    volatile float sink = 0;
    constexpr int kUpdates = 2000000;
    QueryPerformanceCounter(&a);
    for (int n = 0; n < kUpdates; ++n) {
        const steer::Output o = steer::Update(in[n & 63], st);
        sink = sink + o.turn.w;
    }
    QueryPerformanceCounter(&b);
    const double us =
        static_cast<double>(b.QuadPart - a.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart) / kUpdates;
    Report("9", "the time an Update takes", us < 2.0, "%.3f us an Update (< 2)", us);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0 && i + 1 < argc)
            g_verbose = argv[++i];
    std::printf("steertest: src/steer.h, the left hand steering the gun, at 72 Hz. Steered fully within %.0f deg of the "
                "right hand's line, not at all past %.0f; fully from %.0f cm apart, not at all within %.0f; the weight "
                "eased over %.2f s (%.0f%% of the way a frame), the steer at most %.0f deg/s (%.2f a frame).\n",
                steer::kFullAngle / steer::kDegree, steer::kNoneAngle / steer::kDegree,
                (kTogether + steer::kTogetherBlend) * 100, kTogether * 100, steer::kEase,
                (1 - std::exp(-kDt / steer::kEase)) * 100, steer::kMostTurn / steer::kDegree,
                steer::kMostTurn / steer::kDegree * kDt);
    Check1();
    Check2();
    Check3();
    Check4();
    Check5();
    Check6();
    Check7();
    Check8();
    Check9();
    std::printf("%d of %d checks failed\n", g_failed, g_counted);
    return g_failed;
}
