#pragma once

namespace pistolslide {

// The train pistol's slide (The Great Destroyer's M9) worked by the left hand (docs\research\pistol-manual-reload.md).
// Its own, not reload.cpp's charging handle (which stays the M416's):
//   - grabbed by a fresh press of the left trigger (the user's choice, as the M416's handle) with the palm within reach
//     of where one of its three grips holds the slide's rear (pistolposes.h kSlideGrips): S1 overhand, the fingers over
//     its right side (the first one); S2 the fingers over its left side, the hand from its right; S3 a pinch from
//     behind, the index and the thumb. Of those in reach, the one the hand is turned for (out of the palm and along the
//     fingers), so it's grabbed however the hand is turned; the grip it had last is taken again unless another fits
//     clearly better, and once grabbed it's that grip until the hand has eased back off. In reach only of grips the
//     hand isn't turned for, the press waits for the one it is while the hand comes on towards its hold (0.1 s at
//     first, to see; at most 1 s), then takes the best of those in reach: on its way past another grip's hold to its
//     own, the hand isn't given the other grip (its coming on in would pull the slide). A press is the trigger past 0.6
//     after it was last below 0.4, so a trigger resting near 0.6 isn't pressed again and again;
//   - magnetic (the first headset test, 2026-11-01: the press missed at 7.1-9.9 cm from S1's hold, which was all there
//     was, within 7 cm; the user: "make that as magnetic as possible"): within 13 cm, and a press made before the hand
//     gets there waits 0.3 s for it, the trigger held (pistol.txt "grab 13 0.3 0.12": Tune);
//   - the hand drawn glides onto it into that grip (eased over 0.12 s from where it's drawn, its fingers with it),
//     rides it back, and let go eases back to the controller over the same time while the slide springs home out of
//     it; a grab while it's still easing back glides on from where it's drawn then, so nothing jumps. A hand turned
//     more than 90 degrees from where it goes takes longer in proportion (the other way round, about 0.24 s), so it
//     never turns faster than through 90 degrees in the glide's time. The pistol out of hand ends it: nothing to draw
//     the hand on, and back in hand it's where the controller has it;
//   - pulled back along the frame's -z as far as the hand goes, at most its full travel (4.57 cm, the game's own
//     shots), and let go: past 4.0 cm it feeds (racked: the caller tells the mechanism); from the slide stop (locked
//     open on an empty magazine) a tug 4 mm further back drops the stop;
//   - grabbed with the hand still on its way in (the reach and the wait make that the usual grab), its coming isn't a
//     pull: its move along the slide towards the grip's hold, from in front or from behind, is taken up until it gets
//     there or stops coming nearer (2 mm in 0.1 s), and only a move on past the hold pulls (in from in front, the
//     approach itself drew the slide fully back and racked it on the let go). The hand going on forward of home (or
//     the stop) slides forward on it, at most 3 mm a frame (a tracking jump forward and back isn't a pull of the whole
//     jump): the pull is read from where it is then;
//   - the palm lost a moment (the left controller's tracking, a broken pose) with the trigger still pressed: held
//     where it was for up to 0.2 s, then let go (a lost frame far back would otherwise rack it, and once the rounds
//     move a rack with one in the chamber throws it out);
//   - it springs home in 70 ms from fully back (the game's shot cycle comes forward in about 80), or onto the stop;
//   - while the mechanism holds it open (locked) it's drawn there, not where the game's animation has it (the game
//     still has its hidden round, so its own slide goes home after the last shot);
//   - held, the pistol is out of battery: the trigger doesn't fire (input.cpp).
// Pure: no game calls and no logs (Output says what happened), so tools\pistolslidetest.cpp runs it on scripted paths.
// Gun space: the pistol frame bone's (3) bind space (x the gun's left, y up, z to the muzzle; metres). View space: the
// head's (hands.cpp's). The arms' bind space: the bare arms' rest pose.

// The grips, in pistolposes.h kSlideGrips' order.
enum Grip { kOverhand = 0, kFingersLeft = 1, kPinch = 2, kGrips = 3 };

struct Profile {
    float travel = 0.0457f;    // m, fully back (the recordings: z -0.2996 home, -0.3457 back)
    float lockAt = 0.0410f;    // m, held open by the slide stop (to set by eye: the game never shows it)
    float rackAt = 0.0400f;    // m, pulled at least this far and let go: it feeds
    float tug = 0.004f;        // m, from the stop this much further and let go: the stop drops
    float springHome = 0.07f;  // s, from fully back to home
    float coast = 0.2f;        // s, held with the palm lost (the trigger still pressed), then let go
    // The grab goes by the palm centre (the bare left palm bone's vertex middle: hands.cpp kLeftPalmCentre) and each
    // grip's hold (where that grip puts it, moved back with the slide). pistol.txt "grab 13 0.3 0.12" (Tune).
    float reach = 0.13f;  // m: the palm within this of a grip's hold
    float wait = 0.3f;    // s: a press that grabbed nothing waits this long for the hand (the trigger held; 0: not)
    float glide = 0.12f;  // s: the hand drawn gliding on, and easing back off after the let go
    // The grip the hand had last is taken again unless another fits its turn this much better (the fit: out of the palm
    // and along the fingers, dot plus dot, 2 at most). Between S2 and S3 (70 degrees of the hand's turn apart) a hand
    // goes over at 35 degrees, or 7 degrees further on from the one it had last.
    float stick = 0.15f;
    // The glide on (and the ease back off) for a hand turned further than this from where it goes: longer in
    // proportion, so the wrist never turns faster than through this much in the glide's time (the review: turned the
    // other way round from the only grip in reach, 176 degrees in 0.12 s, it whipped round 38 degrees a frame).
    float turnAt = 90.0f;  // degrees
    // Grabbed with the hand still coming to the grip's hold: its move along the slide towards the hold's place is taken
    // up, not pulled, until it hasn't come settleStep nearer the hold for settle (the review: a press 10-13 cm out with
    // the hand coming in from in front at 0.3-0.6 m/s and stopping on the hold drew the slide fully back). A press in
    // reach only of grips the hand isn't turned for waits on the same terms for it to come on to the one it is.
    float settle = 0.1f;        // s
    float settleStep = 0.002f;  // m
    // The hand going on forward of home (or the stop) slides forward on it at most this far a frame (the review: a
    // tracking jump forward a frame and back drew it back by the whole jump, 5 cm racking it on the let go).
    float slip = 0.003f;  // m
};

struct Input {
    float dt = 0.0f;
    const float* pistolPose = nullptr;  // gun space -> view (nullptr: no pistol in hand, or the slide isn't worked)
    // The left hand as its controller holds it (the arms' bind space -> the view: hands.cpp's tracked, before any hold
    // draws it elsewhere; nullptr: not tracked, or busy): its palm centre grabs, its turn picks the grip.
    const float* hand = nullptr;
    float trigger = 0.0f;  // the left controller's, 0-1
    bool locked = false;   // the mechanism holds the slide open (on the slide stop)
};

struct Output {
    float back = -1.0f;  // the slide drawn this far back (m); below 0: as the game's own animation has it
    bool held = false;   // the left hand has it (the trigger doesn't fire)
    // The hand drawn on it (gliding on, riding it, easing back off): the arms' bind space -> the view, to blend in by
    // share over where the hand is drawn (0: not at all; 1 on it), the grip's fingers (pistolposes.h kSlideFingers)
    // with it.
    float hand[12] = {};
    float share = 0.0f;
    int grip = kOverhand;
    // Events this frame.
    bool pressed = false;   // a fresh press of the left trigger with the pistol in hand, grabbed or not (yet)
    float palmFrom = 0.0f;  // ... the palm this far from the nearest grip's hold (m; below 0: no palm, not tracked or
                            // busy); grabbed, at the press or after it: from its grip's hold
    bool grabbed = false;   // at the press (pressed too), or as the palm came within reach while the press waited
    float waited = 0.0f;    // ... that long after the press (s)
    float turnedOff = 0.0f; // ... the hand's turn this far from the grip's (degrees)
    int turnedFor = -1;     // ... the grip the hand is turned for (another than grip: its hold out of reach)
    float glideFor = 0.0f;  // ... the hand drawn glides on over this long (s: the glide, longer for a big turn)
    bool missed = false;    // a press that grabbed nothing has stopped waiting (or never could: Profile::wait 0)
    const char* missWhy = "";
    float nearest = 0.0f;   // ... the nearest the palm came to a grip's hold since the press (m)
    float unseen = 0.0f;    // ... for how long of the wait the palm wasn't known (s: the hand busy or not tracked)
    // Grabbed after waiting, or missed: the palm from the nearest grip's hold at the press. While the hand is still
    // easing back off one grip only that one can be taken, and the nearest goes by it alone.
    float pressedFrom = 0.0f;
    bool letGo = false;
    bool racked = false;    // let go past rackAt, or tugged off the stop: it feeds (the mechanism's Rack)
    float pulled = 0.0f;    // let go: the most this grab pulled it (m, from home)
};

class Slide {
public:
    explicit Slide(const Profile& p = Profile());
    Output Update(const Input& in);
    // The trigger racked it (the mechanism's automatic rack): from the stop it springs home; from home (an empty
    // chamber with the slide forward) it cycles as a shot does, back in 30 ms, then home.
    void Release() {
        m_cycleBack = m_drawn < 0.0f;
        m_springFrom = m_cycleBack ? m_p.travel : m_drawn;
        m_springT = 0.0f;
        m_springing = true;
    }
    // pistol.txt "grab 13 0.3 0.12": the reach (m: 3-25 cm), the wait (s: 0-1) and the glide (s: 0.05-0.5), kept to
    // those.
    void Tune(float reach, float wait, float glide);
    bool Held() const { return m_held; }
    Profile& Settings() { return m_p; }

private:
    // The palm and the hand's turn in gun space this frame.
    struct Hand {
        bool have = false;
        float palm[3] = {}, out[3] = {}, along[3] = {};
        float turn[12] = {};  // the arms' bind space -> gun space
    };
    Hand Read(const Input& in) const;
    // What a hand there would take with the slide at back, of the grips it could take (while it's still easing back
    // off one, only that one).
    struct Picked {
        int grip = -1;            // of those whose hold is in reach, the one it's turned for (-1: none in reach)
        float from = 0.0f;        // ... the palm from its hold (m)
        int turnedFor = -1;       // the one it's turned for, its hold in reach or not
        float turnedFrom = 0.0f;  // ... the palm from its hold (m)
        float nearest = 1e9f;     // the palm from the nearest hold (m)
        bool refused = false;     // a grip it couldn't take had its hold in reach
    };
    Picked Pick(const Hand& h, float back) const;
    void Grab(const Hand& h, int grip, float now, bool locked, Output& o);
    // How long the hand drawn takes to go from a turn to another (rotation | translation rows): the glide, longer for
    // a turn past Profile::turnAt.
    float GlideTime(const float* from, const float* to) const;
    void DrawHand(const float* pistolPose, float handBack, float dt, Output& o);

    Profile m_p;
    bool m_held = false;
    bool m_armed = true;  // the trigger below 0.4 since the last press: past 0.6 is a press
    // A press that grabbed nothing, waiting for the hand: for how long so far (below 0: none), the palm from the
    // nearest grip's hold at the press and the nearest it has come since, how long of it the palm wasn't known, and
    // whether a hold in reach was refused (the hand still easing back off another grip). Coming on towards the hold of
    // the grip it's turned for: which, the palm from that hold at its last step nearer (or further off), how long
    // since; and whether another's hold in reach was passed over for it.
    float m_waited = -1.0f, m_pressFrom = 0.0f, m_nearest = 0.0f, m_unseen = 0.0f;
    bool m_refused = false;
    int m_waitFor = -1;
    float m_waitNear = 0.0f, m_waitStill = 0.0f;
    bool m_passing = false;
    bool m_startLocked = false;
    float m_grabZ = 0.0f, m_start = 0.0f, m_pull = 0.0f, m_maxPull = 0.0f;
    // Grabbed with the hand still coming to the grip's hold: still coming (its move along the slide towards the hold's
    // place taken up, not pulled), the palm from the hold at its last step nearer (or further off), and how long since.
    bool m_coming = false;
    float m_comingFrom = 0.0f, m_comingStill = 0.0f;
    float m_lost = 0.0f;  // s, held with no palm
    bool m_springing = false, m_cycleBack = false;
    float m_springFrom = 0.0f, m_springT = 0.0f;
    float m_drawn = -1.0f;
    // The hand drawn on it: its grip, and the grip of the last grab; gliding on (or riding it) or easing back off, for
    // how long, and how long that glide takes; its share as the glide on began, and where it was drawn then (gun
    // space); its share and where it's drawn now (gun space: the arms' bind space -> gun space), which an ease back off
    // keeps.
    int m_grip = kOverhand, m_lastGrip = -1;
    bool m_handOn = false;
    float m_handFor = 0.0f, m_glideFor = 0.0f;
    float m_shareFrom = 0.0f, m_from[12] = {};
    float m_share = 0.0f, m_shareOff = 0.0f, m_inGun[12] = {};
};

}
