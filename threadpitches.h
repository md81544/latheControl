#pragma once

#include <string>
#include <vector>

namespace mgo
{

struct ThreadPitch
{
    std::string name;
    float pitchMm;
    float maleOd;
    float cutDepthMale;
    float femaleId;
    float cutDepthFemale;
};

const std::vector<ThreadPitch> threadPitches{
        { "Coarse, M3",    0.5f,   3.f, 0.306f,   2.459f,   0.234f },
        { "Coarse, M3.5",  0.6f,  3.5f, 0.368f,   2.850f,   0.282f },
        { "Coarse, M4",    0.7f,   4.f, 0.429f,   3.242f,   0.328f },
        { "Coarse, M5",    0.8f,   5.f, 0.490f,   4.134f,   0.375f },
        { "Coarse, M6",    1.f,    6.f, 0.613f,   4.917f,   0.469f },
        { "Coarse, M8",    1.25f,  8.f, 0.767f,   6.647f,   0.586f },
        { "Coarse, M10",   1.5f,  10.f, 0.920f,   8.376f,   0.704f },
        { "Coarse, M12",   1.75f, 12.f, 1.073f,  10.106f,   0.820f },
        { "Coarse, M14",   2.f,   14.f, 1.227f,  11.835f,   0.938f },
        { "Coarse, M16",   2.f,   16.f, 1.227f,  13.835f,   0.938f },
        { "Coarse, M18",   2.5f,  18.f, 1.533f,  15.394f,   1.072f },
        { "Coarse, M20",   2.5f,  20.f, 1.533f,  17.294f,   1.172f },
        { "Coarse, M22",   2.5f,  22.f, 1.533f,  19.294f,   1.172f }
    };

} // end namespace