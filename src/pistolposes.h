#pragma once

namespace pistolposes {

// The left hand's poses for the train pistol's manual reload (docs\research\pistol-manual-reload.md), fitted on the
// meshes offline (screenshots\wf_reload\design: posefit.exe, then posegen.exe from the pose files). The fingers are as
// hands.cpp gives skin.cpp the bare left hand's (t.bareLeftFingers): each of the left glove's bones, handpose.cpp's
// 0-22, from the bind space relative to its palm (1); skin.cpp DrawBareArms puts them on the bare hand through
// kBareGlove. They're blended in bone by bone about each bone's joint (handpose::Pivot), as PistolSupport's are. So far
// only the slide's: the magazine's carry and flat base come with the magazine by hand.

// S1, the slide racked overhand: the palm on its top, the fingers over its right side, the thumb on its left
// (posefit.exe). The user: "great" (2026-10-31).
constexpr float kSlide[23][12] = {
    {1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f},
    {1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f},
    {0.97447f, -0.21498f, -0.06470f, 0.26363f, 0.15455f, 0.85141f, -0.50121f, 0.20464f, 0.16284f, 0.47841f, 0.86290f, -0.57699f},
    {-0.14302f, -0.78227f, -0.60630f, 1.57568f, -0.05254f, -0.60573f, 0.79393f, 1.52398f, -0.98832f, 0.14541f, 0.04553f, 0.60198f},
    {1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f},
    {1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f},
    {1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f},
    {0.56569f, 0.53317f, -0.62906f, -0.18842f, -0.82397f, 0.39575f, -0.40554f, 1.21299f, 0.03273f, 0.74774f, 0.66319f, -0.75162f},
    {1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f},
    {0.67176f, 0.54485f, -0.50187f, -0.29447f, -0.73731f, 0.55717f, -0.38202f, 0.97943f, 0.07148f, 0.62666f, 0.77601f, -0.66964f},
    {-0.14320f, -0.76182f, -0.63176f, 1.56895f, -0.11451f, -0.62130f, 0.77516f, 1.55669f, -0.98305f, 0.18335f, 0.00173f, 0.58945f},
    {0.56672f, 0.49014f, -0.66226f, -0.14107f, -0.82261f, 0.38177f, -0.42139f, 1.21844f, 0.04629f, 0.78359f, 0.61955f, -0.78713f},
    {1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f},
    {0.54342f, 0.50494f, -0.67062f, -0.15259f, -0.83922f, 0.34566f, -0.41979f, 1.25767f, 0.01984f, 0.79092f, 0.61159f, -0.78347f},
    {1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.00000f},
    {-0.25425f, -0.65634f, -0.71033f, 1.55271f, -0.07885f, -0.71795f, 0.69161f, 1.66217f, -0.96392f, 0.23185f, 0.13079f, 0.50671f},
    {-0.19639f, -0.21077f, -0.95761f, 1.09352f, -0.66369f, -0.69032f, 0.28805f, 2.09128f, -0.72177f, 0.69212f, -0.00431f, -0.11372f},
    {0.98348f, -0.16549f, -0.07328f, 0.20724f, 0.13352f, 0.93674f, -0.32354f, 0.07410f, 0.12219f, 0.30841f, 0.94337f, -0.39208f},
    {0.99096f, -0.11335f, -0.07170f, 0.14495f, 0.10363f, 0.98645f, -0.12719f, -0.01102f, 0.08515f, 0.11861f, 0.98928f, -0.17217f},
    {-0.22863f, -0.00429f, -0.97351f, 0.92479f, -0.71597f, -0.67683f, 0.17113f, 2.13423f, -0.65963f, 0.73613f, 0.15167f, -0.20677f},
    {-0.16989f, -0.19248f, -0.96648f, 1.07987f, -0.70457f, -0.66198f, 0.25569f, 2.08346f, -0.68900f, 0.72439f, -0.02315f, -0.14069f},
    {-0.19951f, -0.09077f, -0.97568f, 1.00334f, -0.71241f, -0.67022f, 0.20803f, 2.11548f, -0.67280f, 0.73659f, 0.06904f, -0.17759f},
    {-0.20728f, -0.69549f, -0.68799f, 1.55967f, -0.11292f, -0.68155f, 0.72301f, 1.63202f, -0.97174f, 0.22755f, 0.06274f, 0.53156f},
};
// S1's palm bone (41) on the pistol's frame bone (3), the slide closed: the bare left hand's placement (hands.cpp
// t.leftPose) is the pistol's pose times this. It slides back with the slide (along the frame's -z).
constexpr float kSlidePalm[12] = {-0.70111f, 0.55312f, -0.45000f, -0.15796f, 0.52516f, 0.82744f,
                                  0.19884f,  -1.12753f, 0.48234f, -0.09691f, -0.87061f, 0.06531f};

}
