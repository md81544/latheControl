#pragma once
// Reading the mill's scales from its DRO sender: the small Arduino box which
// sends their readings to TouchDRO (see hardware/touchdro-sender/README.md).
// The Pi listens to the same serial stream, through a USB-serial adapter

#include "scale.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace mgo {

// Splits the sender's stream into readings. It sends a token per axis, about
// 24 times a second, with no line breaks: "X1234;Y-567;Z89;". Each token is
// a capital letter, then a whole number of counts, then a semicolon. Anything
// else (e.g. a tachometer's "T123/45/6;", or a token cut short when the
// stream is joined part way through) is skipped up to the next semicolon
class DroStreamParser {
public:
    struct Reading {
        char axis;
        int32_t counts;
    };
    // Returns a reading when c completes one
    std::optional<Reading> feed(char c);

private:
    char m_axis { 0 }; // 0 = not in a token we can use
    bool m_negative { false };
    int m_digits { 0 };
    int64_t m_value { 0 };
};

// Reads the sender's serial port in a thread of its own. It isn't real-time,
// and (being started from the main thread) stays off the motors' cores. If
// the port can't be opened (e.g. the adapter is unplugged), it tries again
// every second
class DroSerialReader {
public:
    explicit DroSerialReader(std::string device);
    ~DroSerialReader();
    DroSerialReader(const DroSerialReader&) = delete;
    DroSerialReader& operator=(const DroSerialReader&) = delete;

    // The latest reading for an axis ('A' to 'Z'), or nothing if there
    // hasn't been one for a while
    std::optional<int32_t> latest(char axis) const;

    // A reading is too old to use after this long
    static constexpr std::chrono::milliseconds STALE_AFTER { 1'000 };

private:
    void run();
    // Returns -1 if the port couldn't be opened
    int openPort();

    struct Slot {
        std::atomic<int32_t> counts { 0 };
        // When it was last updated (steady_clock), 0 = never
        std::atomic<int64_t> updatedNs { 0 };
    };

    std::string m_device;
    std::array<Slot, 26> m_slots;
    std::atomic<bool> m_stop { false };
    std::thread m_thread;
};

// One axis of the sender's stream
class DroSerialScale final : public IScale {
public:
    DroSerialScale(const DroSerialReader& reader, char axis)
        : m_reader(reader)
        , m_axis(axis)
    {
    }
    std::optional<int32_t> readCounts() const override
    {
        return m_reader.latest(m_axis);
    }

private:
    const DroSerialReader& m_reader;
    char m_axis;
};

// A stand-in for one axis of the sender, for FAKE builds and the tests. It
// follows a position (e.g. a motor's, in mm), as a scale fitted to that axis
// would, updating about 24 times a second. Like the real sender, it averages
// its recent readings, so lags a little behind a moving axis, and it starts
// from an arbitrary count. setErrorMm() makes it read a different position
// from the one it follows (e.g. to simulate a motor losing steps).
// position() is called from its own thread, so must be thread-safe
class FakeDroScale final : public IScale {
public:
    FakeDroScale(std::function<double()> position, double countsPerMm, bool reversed);
    ~FakeDroScale();
    FakeDroScale(const FakeDroScale&) = delete;
    FakeDroScale& operator=(const FakeDroScale&) = delete;

    std::optional<int32_t> readCounts() const override;
    void setErrorMm(double mm);

    // Where the counts start from, as a real scale's would be
    static constexpr int32_t START_COUNTS = 250'383;

private:
    std::atomic<int32_t> m_counts { START_COUNTS };
    std::atomic<double> m_errorMm { 0.0 };
    std::atomic<bool> m_stop { false };
    std::thread m_thread;
};

} // namespace mgo
