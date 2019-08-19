#pragma once

#include <string>
#include <vector>

namespace mgo
{

struct ThreadPitch
{
    std::string name;
    float pitchMm;
};

// TODO these should come from a config file
// The file should also have major and minor diameters
// to display to aid the user, for both male and female?
// See, for e.g., https://www.newmantools.com/tech/threadm.htm
const std::vector<ThreadPitch> threadPitches{
        { "Off",                0.f   },
        { "Coarse, M3",         0.5f  },
        { "Coarse, M3.5",       0.6f  },
        { "Coarse, M4",         0.7f  },
        { "Coarse, M5",         0.8f  },
        { "Coarse, M6",         1.f   },
        { "Coarse, M8",         1.25f },
        { "Coarse, M10",        1.5f  },
        { "Coarse, M12",        1.75f },
        { "Coarse, M14 & M16",  2.f   },
        { "Coarse, M20 & M22",  2.5f  }
    };

} // end namespace