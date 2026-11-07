#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace mgo {

// The thread sizes offered in threading mode are read from a file at start-up
// (threadpitches.cfg, or as set by ThreadsFile in lc.cfg), so more can be
// added without recompiling. See that file for its format.

// A thread form: its angle, and its depths as fractions of the pitch
enum class ThreadForm {
    Iso, // ISO metric (60 degrees)
    Unified, // UNC, UNF etc (60 degrees)
    Whitworth, // BSW, BSF, BSP (55 degrees)
    Ba // British Association (47.5 degrees)
};

struct ThreadFormInfo {
    const char* name; // as written in the file, e.g. "ISO"
    double angle; // degrees (the included angle between the flanks)
    double maleDepth; // the depth of an external thread (x pitch)
    double femaleDepth; // the depth of an internal thread (x pitch)
    double boreReduction; // an internal thread's bore: major diameter - this x pitch
};

const ThreadFormInfo& threadFormInfo(ThreadForm form);

struct Thread {
    std::string name; // e.g. "M6", or "3/8\" BSW"
    ThreadForm form { ThreadForm::Iso };
    double majorDiameterMm { 0.0 };
    double pitchMm { 0.0 };
    // If given in threads per inch (e.g. 16 for 3/8" BSW)
    std::optional<double> tpi;
    // If the diameter was given in inches
    bool inchDiameter { false };

    // An external thread: the diameter to turn, and the depth to cut
    double maleOd() const;
    double maleDepth() const;
    // An internal thread: the diameter to bore, and the depth to cut
    double femaleBore() const;
    double femaleDepth() const;
};

struct ThreadCategory {
    std::string name; // e.g. "Metric coarse"
    ThreadForm form { ThreadForm::Iso };
    std::vector<Thread> threads;
};

using ThreadTable = std::vector<ThreadCategory>;

// The thread chosen in the thread dialog
struct ThreadChoice {
    std::size_t category { 0 };
    std::size_t thread { 0 };
    bool female { false };
};

// Reads a table of threads in the format of threadpitches.cfg. Lines which
// can't be read are skipped, with a message (giving the line number) added to
// errors. Categories with no threads are left out
ThreadTable readThreads(std::istream& in, std::vector<std::string>& errors);
// The same, from a file (with an error if it can't be opened)
ThreadTable readThreadsFile(const std::string& path, std::vector<std::string>& errors);

// e.g. "1 mm" or "16 tpi (1.5875 mm)"
std::string describePitch(const Thread& thread);

} // end namespace
