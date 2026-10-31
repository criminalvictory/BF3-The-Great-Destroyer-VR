#pragma once
// The left glove's fingers, bent joint by joint. Found 2026-10-30 in the gloves' mesh dump: four
// bones a finger (one in the palm, three outwards) and three for the thumb, with their joints where
// the bones' weights blend. tools/handview.cpp draws these poses without the game.

#include <cstddef>
#include <cstdint>

namespace handpose {

constexpr int kBones = 23;  // the left hand's bones in the gloves' draw (0-22)

// How the hand is held, each 0-1.
struct Fingers {
    float curl[4] = {};   // index, middle, ring, little: 0 relaxed (a little bent), 1 rolled into a fist
    float thumb = 0.0f;   // 0 relaxed at the side, 1 folded across the fingers
    float spread = 0.0f;  // 0 side by side, 1 wide apart (less as they curl)
    float apart[4] = {};  // more degrees each turns towards the thumb at its knuckle (the lever's V)
};

// Each bone's move in the gloves' bind space (3x4, rows: rotation | translation), the palm widened
// by width (1: as the mesh is) with the fingers moved apart on it. The glove is drawn with its
// placement composed with these.
void Pose(const Fingers& fingers, float width, float out[kBones][12]);

// The wrist's cuff (bones 0, 4 and 5) widened as Pose widens it for a palm width wide: half as much,
// across the hand about its middle (3x4, rows: rotation | translation, in the bind space). hands.cpp
// draws the solved forearm's frames composed with it (armik.h), so the cuff on the forearm is as wide
// as on the hand.
void CuffWiden(float width, float out[12]);

// Bends each finger of an already posed hand further (degrees; negative opens it): bend[0] the
// thumb ... bend[4] the little finger, spread over its three joints (the middle one most), about
// where those joints are in that pose. bones: from the gloves' bind space into anything, as the
// glove is drawn with them; changed in place. (The game's own grip on the gun, its fingers moved a
// little with the controller's: hands.cpp.)
void BendFurther(float bones[kBones][12], const float bend[5]);

// For tools/handview.cpp's checks: each finger's joints in the bind space, from the palm out (the
// thumb's base, knuckle and last joint; the others' knuckle, middle and last joint), then its tip.
void Joints(float out[5][4][3]);
// For tools/handview.cpp's tuning, in degrees: "thumb" the folded thumb (across in front of the
// palm, then bent at its base, knuckle and last joint), "fist1".."fist4" a curled finger (index ...
// little: at its knuckle, middle and last joint), "curlspread" the curled fingers' turn towards the
// thumb (index ... little); "slim" how much of their width the fingers keep (1: all).
void Tune(const char* name, const float* values, int count);

// The right glove: bones 23-44 of the gloves' draw (its root 37), the left's mirror image in x to
// within 3 mm but numbered apart. Found 2026-10-31 in the same mesh dump, each bone by its vertices'
// centre against the left's mirrored; its joints and the axes its fingers bend about, from the
// game's own grip on the gun (handpose.cpp).
constexpr int kRightFirst = 23, kRightBones = 22;
// Each of its bones' finger (0 the thumb ... 4 the little finger; -1 the wrist and palm) and how
// many joints lie between it and the palm (0: a bone of the palm), from bone 23 on. The index
// finger (the trigger finger): 24 in the palm, then 26, 38 and 31 out to the tip.
inline constexpr int8_t kRightFinger[kRightBones] = {2, 1, 2, 1, 3, 3, 4, 4, 1, -1, 4,
                                                     4, 0, 0, -1, 1, 0, 3, 3, 2, 2, -1};
inline constexpr int8_t kRightDepth[kRightBones] = {0, 0, 1, 1, 1, 0, 1, 0, 3, 0, 3, 2, 1, 2, 0, 2, 3, 3, 2, 2, 3, 0};

// Bends the right glove's fingers further, as BendFurther the left's: bend[0] the thumb ... bend[4]
// the little finger, degrees towards the palm (negative opens it), spread over its three joints
// about where they are in its pose. palette: the gloves' draw's bones by their numbers there
// (0-59), each from the bind space to wherever the glove is drawn (3x4, rows: rotation |
// translation); changed in place, only the bent fingers' bones. A draw whose palette is numbered
// its own way (a depth pass) gives map: for each of its entries, the gloves' bone it is (-1 none),
// as kGloveDepthRight and kBodyDepthRight below; bones it hasn't are left out (and a finger without
// its first bone isn't bent).
void BendRight(float palette[][12], const float bend[5], const int8_t* map = nullptr, size_t count = 0);
// Its index finger alone: the trigger pulled (degrees towards the palm, from the game's grip).
void BendRightIndex(float palette[][12], float degrees, const int8_t* map = nullptr, size_t count = 0);
// For tools/handview.cpp's checks: the right glove's joints, as Joints the left's, and the axis
// each finger bends about (in the bind space; positive turns curl it towards the palm).
void RightJoints(float out[5][4][3]);
void RightAxes(float out[5][3]);

// The right hand in the two depth passes that draw it with palettes of their own, so a bent finger
// bends there too (or the depth pass shows it unbent): for each of their palette entries (as many as
// their roles in skin.cpp's kArmDraws), the gloves' bone it is (-1: none, or not the right hand's),
// matched by vertices at the same place in the mesh dumps (the matching that gives kGloveDepthLeft
// and kBodyDepthLeft again). The gloves' depth pass (7617 indices from 49995) has every bone of the
// right hand; the body's (21189 from 28806) only some of the middle and ring fingers (and a vertex
// a little on the little finger's), four of its vertices weighted a little to the index finger's
// first bone (26).
inline constexpr int8_t kGloveDepthRight[] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                              -1, -1, -1, 44, 37, 32, 23, 25, 42, 43, 28, 27, 41,
                                              40, 30, 29, 34, 33, 24, 26, 38, 31, 35, 36, 39, -1};
inline constexpr int8_t kBodyDepthRight[] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                             -1, -1, -1, -1, -1, 23, 25, 42, 28, 27, 29, 26, -1};

}
