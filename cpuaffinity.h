#pragma once

#include <set>

namespace mgo {

// Keeps every thread of this process which exists now (e.g. pigpio's and Qt's)
// off the given CPU cores, which are reserved for the motor threads. Threads
// created later inherit the setting from the thread which creates them.
// Linux only; returns false if it couldn't be done (or on another OS)
bool keepThreadsOffCores(const std::set<int>& reservedCores);

} // namespace mgo
