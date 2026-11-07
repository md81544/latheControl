#include "threadpitches.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <istream>

namespace mgo {

namespace {

constexpr double MM_PER_INCH = 25.4;

// The forms' constants. The depths are of the basic profile: for ISO and
// Unified, 0.61343 x pitch for an external thread (to the root, as rounded)
// and 0.54127 (5/8 of the triangle's height) for an internal one, whose bore
// is the basic minor diameter. Whitworth and BA forms are symmetrical, so the
// depths are the same
const std::array<ThreadFormInfo, 4> FORMS { {
    { "ISO", 60.0, 0.61343, 0.54127, 1.08253 },
    { "UN", 60.0, 0.61343, 0.54127, 1.08253 },
    { "Whitworth", 55.0, 0.640327, 0.640327, 1.280654 },
    { "BA", 47.5, 0.6, 0.6, 1.2 },
} };

std::string trim(const std::string& s)
{
    const auto start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// A number, which may be a fraction (e.g. "3/8"); nullopt if it isn't one
std::optional<double> parseNumber(const std::string& text)
{
    const auto number = [](const std::string& t) -> std::optional<double> {
        if (t.empty()) {
            return std::nullopt;
        }
        char* end = nullptr;
        const double value = std::strtod(t.c_str(), &end);
        if (end != t.c_str() + t.size()) {
            return std::nullopt; // (not all of it is a number)
        }
        return value;
    };
    if (const auto slash = text.find('/'); slash != std::string::npos) {
        const auto top = number(trim(text.substr(0, slash)));
        const auto bottom = number(trim(text.substr(slash + 1)));
        if (!top || !bottom || *bottom == 0.0) {
            return std::nullopt;
        }
        return *top / *bottom;
    }
    return number(text);
}

// Removes suffix (case-insensitive) from the end of text, if it's there
bool removeSuffix(std::string& text, const std::string& suffix)
{
    if (text.size() >= suffix.size()
        && lower(text.substr(text.size() - suffix.size())) == suffix) {
        text = trim(text.substr(0, text.size() - suffix.size()));
        return true;
    }
    return false;
}

std::string formatNumber(double value, int maxDecimals)
{
    std::string s = fmt::format("{:.{}f}", value, maxDecimals);
    if (s.find('.') != std::string::npos) {
        s.erase(s.find_last_not_of('0') + 1);
        if (s.back() == '.') {
            s.pop_back();
        }
    }
    return s;
}

} // end anonymous namespace

const ThreadFormInfo& threadFormInfo(ThreadForm form)
{
    return FORMS.at(static_cast<std::size_t>(form));
}

double Thread::maleOd() const
{
    return majorDiameterMm;
}

double Thread::maleDepth() const
{
    return threadFormInfo(form).maleDepth * pitchMm;
}

double Thread::femaleBore() const
{
    return majorDiameterMm - threadFormInfo(form).boreReduction * pitchMm;
}

double Thread::femaleDepth() const
{
    return threadFormInfo(form).femaleDepth * pitchMm;
}

ThreadTable readThreads(std::istream& in, std::vector<std::string>& errors)
{
    ThreadTable table;
    std::string line;
    int lineNumber = 0;
    // Set after a category which couldn't be read: its threads are skipped
    // (rather than going in the category before, with the wrong form)
    bool skipping = false;
    const auto error = [&](const std::string& message) {
        errors.push_back(fmt::format("line {}: {}", lineNumber, message));
    };
    while (std::getline(in, line)) {
        ++lineNumber;
        line = trim(line);
        // (Only whole lines are comments, as names may contain a #)
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            // A category: [name | form]
            const auto close = line.find(']');
            const auto bar = line.find('|');
            skipping = true;
            if (close == std::string::npos || bar == std::string::npos || bar > close) {
                error("a category should be like [Metric coarse | ISO] (its threads are skipped)");
                continue;
            }
            const std::string name = trim(line.substr(1, bar - 1));
            const std::string formName = lower(trim(line.substr(bar + 1, close - bar - 1)));
            const auto form = std::find_if(FORMS.begin(), FORMS.end(), [&](const auto& f) {
                return lower(f.name) == formName;
            });
            if (name.empty() || form == FORMS.end()) {
                error("unknown thread form: use ISO, UN, Whitworth or BA (its threads are skipped)");
                continue;
            }
            skipping = false;
            table.push_back({ name, static_cast<ThreadForm>(form - FORMS.begin()), {} });
            continue;
        }
        // A thread: name, diameter, pitch
        if (skipping) {
            continue;
        }
        if (table.empty()) {
            error("a thread before any category");
            continue;
        }
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (;;) {
            const auto comma = line.find(',', start);
            fields.push_back(trim(line.substr(start, comma - start)));
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        if (fields.size() != 3 || fields.at(0).empty()) {
            error("a thread should be like: name, diameter, pitch");
            continue;
        }
        Thread thread;
        thread.name = fields.at(0);
        thread.form = table.back().form;
        std::string diameter = fields.at(1);
        if (removeSuffix(diameter, "\"") || removeSuffix(diameter, "in")) {
            thread.inchDiameter = true;
        } else {
            removeSuffix(diameter, "mm");
        }
        const auto diameterValue = parseNumber(diameter);
        std::string pitch = fields.at(2);
        const bool tpi = removeSuffix(pitch, "tpi");
        if (!tpi) {
            removeSuffix(pitch, "mm");
        }
        const auto pitchValue = parseNumber(pitch);
        if (!diameterValue || *diameterValue <= 0.0) {
            error("the diameter should be a number (in mm, or inches with a \")");
            continue;
        }
        if (!pitchValue || *pitchValue <= 0.0) {
            error("the pitch should be a number (in mm, or threads per inch with tpi)");
            continue;
        }
        thread.majorDiameterMm = thread.inchDiameter ? *diameterValue * MM_PER_INCH : *diameterValue;
        if (tpi) {
            thread.tpi = *pitchValue;
            thread.pitchMm = MM_PER_INCH / *pitchValue;
        } else {
            thread.pitchMm = *pitchValue;
        }
        table.back().threads.push_back(thread);
    }
    std::erase_if(table, [](const ThreadCategory& c) { return c.threads.empty(); });
    return table;
}

ThreadTable readThreadsFile(const std::string& path, std::vector<std::string>& errors)
{
    std::ifstream file(path);
    if (!file) {
        errors.push_back(fmt::format("couldn't open {}", path));
        return {};
    }
    return readThreads(file, errors);
}

std::string describePitch(const Thread& thread)
{
    if (thread.tpi) {
        return fmt::format(
            "{} tpi ({} mm)", formatNumber(*thread.tpi, 2), formatNumber(thread.pitchMm, 4));
    }
    return fmt::format("{} mm", formatNumber(thread.pitchMm, 4));
}

} // end namespace
