#pragma once
// The left glove's fingers, bent joint by joint. Found 2026-10-30 in the gloves' mesh dump: four
// bones a finger (one in the palm, three outwards) and three for the thumb, with their joints where
// the bones' weights blend. tools/handview.cpp draws these poses without the game.

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

}
