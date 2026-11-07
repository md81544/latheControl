#include "cpuaffinity.h"

#include "log.h" // IWYU pragma: keep (MGOLOG, in the Linux-only code)

#ifdef __linux__
#include <filesystem>
#include <sched.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace mgo {

#ifdef __linux__

bool keepThreadsOffCores(const std::set<int>& reservedCores)
{
    const long coreCount = ::sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    int allowed = 0;
    for (int core = 0; core < coreCount && core < CPU_SETSIZE; ++core) {
        if (!reservedCores.contains(core)) {
            CPU_SET(core, &cpus);
            ++allowed;
        }
    }
    if (allowed == 0) {
        MGOLOG("Not reserving CPU cores for the motors: that would leave none for anything else");
        return false;
    }
    // Each thread has its own affinity, so set it for all of them (listed in
    // /proc/self/task by thread ID)
    bool ok = true;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", ec)) {
        const pid_t tid = std::stoi(entry.path().filename().string());
        if (::sched_setaffinity(tid, sizeof(cpus), &cpus) != 0) {
            ok = false;
        }
    }
    return ok && !ec;
}

#else // not Linux

bool keepThreadsOffCores(const std::set<int>&)
{
    return false;
}

#endif

} // namespace mgo
