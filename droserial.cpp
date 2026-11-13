#include "droserial.h"

#include "log.h"

#include <fmt/format.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace mgo {

namespace {

int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

std::optional<DroStreamParser::Reading> DroStreamParser::feed(char c)
{
    if (c >= 'A' && c <= 'Z') {
        m_axis = c;
        m_negative = false;
        m_digits = 0;
        m_value = 0;
        return std::nullopt;
    }
    if (c == ';') {
        std::optional<Reading> reading;
        if (m_axis != 0 && m_digits > 0) {
            reading = Reading { m_axis, static_cast<int32_t>(m_negative ? -m_value : m_value) };
        }
        m_axis = 0;
        return reading;
    }
    if (m_axis == 0) {
        return std::nullopt;
    }
    if (c == '-' && m_digits == 0 && !m_negative) {
        m_negative = true;
    } else if (c >= '0' && c <= '9' && m_digits < 9) {
        // (Nine digits fit easily in 32 bits; the scales' counts have seven
        // at most)
        m_value = m_value * 10 + (c - '0');
        ++m_digits;
    } else {
        // Not a token we know: skip to the next semicolon
        m_axis = 0;
    }
    return std::nullopt;
}

DroSerialReader::DroSerialReader(std::string device)
    : m_device(std::move(device))
{
    m_thread = std::thread([this]() { run(); });
}

DroSerialReader::~DroSerialReader()
{
    m_stop = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::optional<int32_t> DroSerialReader::latest(char axis) const
{
    if (axis < 'A' || axis > 'Z') {
        return std::nullopt;
    }
    const Slot& slot = m_slots.at(static_cast<std::size_t>(axis - 'A'));
    const int64_t updated = slot.updatedNs;
    if (updated == 0
        || nowNs() - updated
            > std::chrono::duration_cast<std::chrono::nanoseconds>(STALE_AFTER).count()) {
        return std::nullopt;
    }
    return slot.counts.load();
}

int DroSerialReader::openPort()
{
    // (O_NONBLOCK so that opening doesn't wait for a modem's carrier signal;
    // reads block, for up to VTIME, once it's set up)
    const int fd = open(m_device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    termios tty {};
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return -1;
    }
    // 9600 baud, 8 data bits, no parity, one stop bit, no flow control,
    // and no processing of the characters
    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(CSTOPB | CRTSCTS);
    // A read returns whatever has arrived, or nothing after 0.2 s, so the
    // thread can see when it's asked to stop
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 2;
    if (tcsetattr(fd, TCSANOW, &tty) != 0 || fcntl(fd, F_SETFL, 0) != 0) {
        close(fd);
        return -1;
    }
    tcflush(fd, TCIFLUSH);
    return fd;
}

void DroSerialReader::run()
{
    DroStreamParser parser;
    bool reportedFailure = false;
    while (!m_stop) {
        const int fd = openPort();
        if (fd < 0) {
            if (!reportedFailure) {
                MGOLOG(
                    fmt::format(
                        "*** Warning *** can't open the scales' serial port {}: {}; will keep "
                        "trying",
                        m_device,
                        std::strerror(errno)));
                reportedFailure = true;
            }
            for (int i = 0; i < 10 && !m_stop; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
        MGOLOG(fmt::format("Reading the scales from {}", m_device));
        reportedFailure = false;
        parser = DroStreamParser();
        std::array<char, 256> buffer;
        while (!m_stop) {
            const ssize_t n = read(fd, buffer.data(), buffer.size());
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                // e.g. the adapter was unplugged
                MGOLOG(
                    fmt::format(
                        "*** Warning *** error reading the scales' serial port {}: {}",
                        m_device,
                        std::strerror(errno)));
                break;
            }
            for (ssize_t i = 0; i < n; ++i) {
                if (const auto reading = parser.feed(buffer[static_cast<std::size_t>(i)])) {
                    Slot& slot = m_slots.at(static_cast<std::size_t>(reading->axis - 'A'));
                    slot.counts = reading->counts;
                    slot.updatedNs = nowNs();
                }
            }
        }
        close(fd);
    }
}

FakeDroScale::FakeDroScale(std::function<double()> position, double countsPerMm, bool reversed)
{
    m_thread = std::thread([this, position = std::move(position), countsPerMm, reversed]() {
        // A third of a second's readings. (A guess at the real sender's lag,
        // until lc.log shows how long its readings take to settle)
        constexpr std::size_t AVERAGED = 8;
        std::deque<double> recent;
        while (!m_stop) {
            const double counts = (position() + m_errorMm) * countsPerMm * (reversed ? -1 : 1);
            recent.push_back(counts);
            if (recent.size() > AVERAGED) {
                recent.pop_front();
            }
            double total = 0.0;
            for (double r : recent) {
                total += r;
            }
            m_counts = START_COUNTS + static_cast<int32_t>(std::lround(total / recent.size()));
            std::this_thread::sleep_for(std::chrono::milliseconds(42));
        }
    });
}

FakeDroScale::~FakeDroScale()
{
    m_stop = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::optional<int32_t> FakeDroScale::readCounts() const
{
    return m_counts.load();
}

void FakeDroScale::setErrorMm(double mm)
{
    m_errorMm = mm;
}

} // namespace mgo
