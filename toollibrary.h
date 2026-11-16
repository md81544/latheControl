#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mgo {

// The tool library (lathe; ToolLibrary in the config): the tools used in the
// quick-change toolpost, each with its offsets from the reference tool (tool
// 1, the most-used tool). Changing tool then shifts the axes' positions by the
// difference between the two tools' offsets, so a position (a memory slot, the
// current feature, a multi-pass's start) means the same place on the work
// whichever tool is in use.
//
// The offsets only hold while the toolpost isn't rotated on the carriage:
// moving it without rotating it moves every tool by the same amount, so their
// offsets from each other stay the same (though the positions set on the work
// don't). So far only external tools can be measured (touched on the work's
// outside diameter and its end).

struct Tool {
    int number { 1 };
    std::string description;
    double noseRadiusMm { 0.0 };
    // Where this tool's tip is, relative to the reference tool's, with the
    // toolpost in the same place, in each axis's positions (so X's is along
    // its travel: a radius, not a diameter). The reference tool's are 0
    double axis1OffsetMm { 0.0 };
    double axis2OffsetMm { 0.0 };
    // When the offsets were last measured (YYYY-MM-DD), or empty if never
    std::string measured;
};

// Where a tool's tip touched the work, as the axes' positions when it did: on
// the work's end (axis 1) and on its diameter (axis 2)
struct ToolTouches {
    double axis1Mm { 0.0 };
    double axis2Mm { 0.0 };
};

class ToolLibrary {
public:
    static constexpr int REFERENCE_TOOL = 1;
    static constexpr int MAX_TOOL = 99;
    // A tool which isn't in the library (e.g. one not measured yet, or one
    // whose offsets can't be measured, such as a knurling tool). Its offsets
    // aren't known, so changing to it can't shift the positions
    static constexpr int UNKNOWN_TOOL = 0;

    // A library with only the reference tool, which is current
    ToolLibrary();

    // In number order. Always includes the reference tool
    const std::vector<Tool>& tools() const;
    // nullptr if there's no such tool
    const Tool* find(int number) const;
    // The tool in the toolpost: for the unknown tool, numbered UNKNOWN_TOOL,
    // described as "Unknown tool", with no offsets or nose radius
    const Tool& current() const;
    bool currentIsKnown() const;
    // Makes a tool current (or the unknown tool). False (changing nothing) if
    // there's no such tool
    bool setCurrent(int number);
    // The lowest number not used yet, or 0 if all are
    int nextFreeNumber() const;
    // Adds a tool, or replaces the one with its number. If it's the reference
    // tool with offsets (e.g. measured again, after its tip has been changed),
    // every tool's offsets move by the same amount, so the reference tool's
    // are 0 again and the others' are from its new tip
    void store(Tool tool);

    // The offsets (axis 1, axis 2) of a tool measured from another tool
    // (from), whose offsets are known: from touched the work at fromTouches,
    // then the tool being measured touched the same surfaces at toolTouches,
    // with the positions still as from's. The further the tool had to go to
    // touch them, the further back its tip is
    static std::pair<double, double> measuredOffsets(
        const Tool& from,
        const ToolTouches& fromTouches,
        const ToolTouches& toolTouches);

    // The library as the file's text (axis labels for its comments, e.g. Z
    // and X), and read back from it. Lines which can't be read are left
    // out, each with an error (with its line number)
    std::string
    toText(const std::string& axis1Label = "Z", const std::string& axis2Label = "X") const;
    static ToolLibrary fromText(std::string_view text, std::vector<std::string>& errors);

private:
    std::vector<Tool> m_tools;
    int m_current { REFERENCE_TOOL };
};

// Reads the library from a file. A missing file isn't an error: it gives a
// new library, with only the reference tool
ToolLibrary readToolLibrary(const std::string& path, std::vector<std::string>& errors);

// Writes the library to a file: to a temporary file first, then renamed over
// it, so a power cut can't leave it half written. The file it replaces is kept
// as path.bak. Returns why it couldn't be written, or empty
std::string writeToolLibrary(
    const ToolLibrary& library,
    const std::string& path,
    const std::string& axis1Label = "Z",
    const std::string& axis2Label = "X");

} // namespace mgo
