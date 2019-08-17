#pragma once

#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>

namespace mgo
{

class Logger
{
public:
    explicit Logger(const std::string& filename);
    ~Logger();

    void Log(
        std::string const &message,
        char const *function,
        char const *file,
        int line
        );

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    std::ofstream m_log;
    std::mutex m_mutex;
};

extern Logger* g_logger;

} // namespace mgo


#define INIT_MGOLOG( filename_ ) \
    mgo::g_logger = new mgo::Logger( filename_ );


#define MGOLOG( Message_ )                                    \
    do {                                                      \
    if (mgo::g_logger) {                                      \
    mgo::g_logger->Log( static_cast<std::ostringstream &>(    \
        std::ostringstream().flush() << Message_ ).str(),     \
        __FUNCTION__, __FILE__, __LINE__ );                   \
    } } while(0)

// Use MGOLOG_DEBUG if you want to have logging be a NoOp in Release mode
#ifdef NDEBUG
#define MGOLOG_DEBUG( _ )                                     \
    do                                                        \
    {                                                         \
    } while ( 0 );
#else
#define MGOLOG_DEBUG( Message_ ) MGOLOG( Message_ )
#endif

