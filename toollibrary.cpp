#include "toollibrary.h"

#include <fmt/format.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace mgo {

namespace {

constexpr const char* REFERENCE_DESCRIPTION = "Reference tool";

std::string trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return "";
    }
    const auto last = s.find_last_not_of(" \t\r");
    return std::string(s.substr(first, last - first + 1));
}

// A description as it can be written on one line of the file
std::string oneLine(std::string s)
{
    std::replace(s.begin(), s.end(), '\n', ' ');
    std::replace(s.begin(), s.end(), '\r', ' ');
    return trim(s);
}

// The whole of s as a number, or nullopt
std::optional<double> parseDouble(const std::string& s)
{
    try {
        std::size_t used = 0;
        const double d = std::stod(s, &used);
        if (used == s.size()) {
            return d;
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<int> parseInt(const std::string& s)
{
    try {
        std::size_t used = 0;
        const int n = std::stoi(s, &used);
        if (used == s.size()) {
            return n;
        }
    } catch (...) {
    }
    return std::nullopt;
}

} // end anonymous namespace

ToolLibrary::ToolLibrary()
    : m_tools { Tool { .number = REFERENCE_TOOL, .description = REFERENCE_DESCRIPTION } }
{
}

const std::vector<Tool>& ToolLibrary::tools() const
{
    return m_tools;
}

const Tool* ToolLibrary::find(int number) const
{
    const auto it = std::find_if(
        m_tools.begin(), m_tools.end(), [number](const Tool& t) { return t.number == number; });
    return it == m_tools.end() ? nullptr : &*it;
}

const Tool& ToolLibrary::current() const
{
    static const Tool unknown { .number = UNKNOWN_TOOL, .description = "Unknown tool" };
    if (m_current == UNKNOWN_TOOL) {
        return unknown;
    }
    // (Otherwise the current tool is always in the library: see setCurrent())
    return *find(m_current);
}

bool ToolLibrary::currentIsKnown() const
{
    return m_current != UNKNOWN_TOOL;
}

bool ToolLibrary::setCurrent(int number)
{
    if (number != UNKNOWN_TOOL && !find(number)) {
        return false;
    }
    m_current = number;
    return true;
}

int ToolLibrary::nextFreeNumber() const
{
    for (int n = REFERENCE_TOOL; n <= MAX_TOOL; ++n) {
        if (!find(n)) {
            return n;
        }
    }
    return 0;
}

void ToolLibrary::store(Tool tool)
{
    tool.description = oneLine(tool.description);
    if (tool.number == REFERENCE_TOOL && (tool.axis1OffsetMm != 0.0 || tool.axis2OffsetMm != 0.0)) {
        // Offsets are from the reference tool's tip, which has moved
        for (Tool& t : m_tools) {
            t.axis1OffsetMm -= tool.axis1OffsetMm;
            t.axis2OffsetMm -= tool.axis2OffsetMm;
        }
        tool.axis1OffsetMm = 0.0;
        tool.axis2OffsetMm = 0.0;
    }
    const auto it = std::find_if(
        m_tools.begin(), m_tools.end(), [&tool](const Tool& t) { return t.number == tool.number; });
    if (it != m_tools.end()) {
        *it = std::move(tool);
        return;
    }
    m_tools.push_back(std::move(tool));
    std::sort(m_tools.begin(), m_tools.end(), [](const Tool& a, const Tool& b) {
        return a.number < b.number;
    });
}

ToolOffsets ToolLibrary::measuredOffsets(
    const Tool& from,
    const ToolTouches& fromTouches,
    const ToolTouches& toolTouches)
{
    // With the positions as from's, the tool's tip reads where from's would
    // be. It touched the surface at toolTouches, where from's touched at
    // fromTouches: so its tip is (fromTouches - toolTouches) from from's
    return { .axis1Mm = from.axis1OffsetMm + fromTouches.axis1Mm - toolTouches.axis1Mm,
             .axis2Mm = from.axis2OffsetMm + fromTouches.axis2Mm - toolTouches.axis2Mm };
}

std::string ToolLibrary::toText(const std::string& axis1Label, const std::string& axis2Label) const
{
    std::ostringstream s;
    s << "# latheControl's tool library. The program writes this file, so any\n"
         "# comments added here are lost; values can be changed while it isn't\n"
         "# running.\n"
         "#\n"
         "# Each tool's offsets are where its tip is relative to the reference\n"
         "# tool's (tool 1), in mm, with the toolpost in the same place. "
      << axis2Label << "'s is\n"
      << "# along its travel (a radius, not a diameter). One tool per line:\n"
      << "#   number | " << axis1Label << " offset | " << axis2Label
      << " offset | nose radius | when measured | description\n"
         "\n"
      << "# The tool in use (\"unknown\" if it isn't one of these)\n"
      << "Current = "
      << (m_current == UNKNOWN_TOOL ? std::string("unknown") : std::to_string(m_current)) << "\n\n";
    for (const Tool& t : m_tools) {
        s << fmt::format(
            "{} | {:.4f} | {:.4f} | {:.3f} | {} | {}\n",
            t.number,
            t.axis1OffsetMm,
            t.axis2OffsetMm,
            t.noseRadiusMm,
            t.measured,
            t.description);
    }
    return s.str();
}

ToolLibrary ToolLibrary::fromText(std::string_view text, std::vector<std::string>& errors)
{
    ToolLibrary library;
    std::optional<int> current;
    int lineNumber = 0;
    std::istringstream in { std::string(text) };
    std::string line;
    while (std::getline(in, line)) {
        ++lineNumber;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }
        const auto error = [&errors, lineNumber](const std::string& what) {
            errors.push_back(fmt::format("Line {}: {}", lineNumber, what));
        };
        if (trimmed.starts_with("Current")) {
            const auto equals = trimmed.find('=');
            const std::string value
                = equals == std::string::npos ? "" : trim(trimmed.substr(equals + 1));
            const auto n = value == "unknown" ? std::optional<int>(UNKNOWN_TOOL) : parseInt(value);
            if (!n) {
                error("expected \"Current = <tool number>\" (or unknown)");
                continue;
            }
            current = *n;
            continue;
        }
        // Five fields separated by |, then the description (which may
        // itself contain a |)
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (int field = 0; field < 5; ++field) {
            const auto bar = trimmed.find('|', start);
            if (bar == std::string::npos) {
                break;
            }
            fields.push_back(trim(std::string_view(trimmed).substr(start, bar - start)));
            start = bar + 1;
        }
        if (fields.size() < 5) {
            error("expected number | offset | offset | nose radius | when measured | description");
            continue;
        }
        fields.push_back(trim(std::string_view(trimmed).substr(start)));
        const auto number = parseInt(fields.at(0));
        const auto axis1 = parseDouble(fields.at(1));
        const auto axis2 = parseDouble(fields.at(2));
        const auto nose = parseDouble(fields.at(3));
        if (!number || *number < REFERENCE_TOOL || *number > MAX_TOOL) {
            error(fmt::format("the tool number must be {} to {}", REFERENCE_TOOL, MAX_TOOL));
            continue;
        }
        if (!axis1 || !axis2 || !nose || *nose < 0.0) {
            error("the offsets and nose radius must be numbers (the nose radius 0 or more)");
            continue;
        }
        if (*number != REFERENCE_TOOL && library.find(*number)) {
            error(fmt::format("tool {} is listed more than once", *number));
            continue;
        }
        Tool tool { .number = *number,
                    .description = fields.at(5),
                    .noseRadiusMm = *nose,
                    .axis1OffsetMm = *axis1,
                    .axis2OffsetMm = *axis2,
                    .measured = fields.at(4) };
        if (*number == REFERENCE_TOOL) {
            // (Its offsets are 0 by definition)
            if (*axis1 != 0.0 || *axis2 != 0.0) {
                error("the reference tool's offsets must be 0 (they're ignored)");
            }
            tool.axis1OffsetMm = 0.0;
            tool.axis2OffsetMm = 0.0;
            if (tool.description.empty()) {
                tool.description = REFERENCE_DESCRIPTION;
            }
        }
        library.store(std::move(tool));
    }
    if (current && !library.setCurrent(*current)) {
        errors.push_back(fmt::format("The current tool ({}) isn't in the library", *current));
    }
    return library;
}

ToolLibrary readToolLibrary(const std::string& path, std::vector<std::string>& errors)
{
    std::ifstream in(path);
    if (!in) {
        if (std::filesystem::exists(path)) {
            errors.push_back(fmt::format("Couldn't open {}", path));
        }
        return ToolLibrary();
    }
    std::ostringstream text;
    text << in.rdbuf();
    std::vector<std::string> lineErrors;
    ToolLibrary library = ToolLibrary::fromText(text.str(), lineErrors);
    for (const auto& e : lineErrors) {
        errors.push_back(path + ": " + e);
    }
    return library;
}

std::string writeToolLibrary(
    const ToolLibrary& library,
    const std::string& path,
    const std::string& axis1Label,
    const std::string& axis2Label)
{
    const std::string temporary = path + ".new";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) {
            return fmt::format("Couldn't write {}: {}", temporary, std::strerror(errno));
        }
        out << library.toText(axis1Label, axis2Label);
        out.close();
        if (!out) {
            return fmt::format("Couldn't write {}", temporary);
        }
    }
    // (On to the SD card before the rename, so the new name never refers to a
    // file whose contents haven't been written yet)
    if (const int fd = ::open(temporary.c_str(), O_RDONLY); fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::copy_file(
            path, path + ".bak", std::filesystem::copy_options::overwrite_existing, ec);
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        return fmt::format("Couldn't replace {}: {}", path, ec.message());
    }
    return "";
}

} // namespace mgo
