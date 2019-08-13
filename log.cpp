#include "log.h"

#include <chrono>
#include <iomanip>

namespace
{

std::string NowAsString()
{
    // Obtaining the current time with std::chrono::system_clock::now()
    // provides us with a std::chrono::time_point
    // To use std::put_time we need a std::tm structure. This
    // does not store fractions of secs. We need to append the microeconds
    // separately to the output.
    // Note g++ 5.0 required for put_time; it remains unimplemented in 4.9

    using namespace std::chrono;

    // Get current time with fractions (as std::chrono::time_point):
    const auto currentTime = system_clock::now();

    // Convert to time_t (loses fractions)
    time_t time = system_clock::to_time_t( currentTime );

    // Convert time_t back to time_point
    auto currentTimeRounded = system_clock::from_time_t( time );

    // If std::time_t has lower precision, it is implementation-
    // defined whether the value is rounded or truncated, hence :
    if ( currentTimeRounded > currentTime )
    {
        --time;
        currentTimeRounded -= seconds( 1 );
    }
    long long microseconds = duration_cast<duration<int, std::micro>>(
                           currentTime - currentTimeRounded )
                           .count();

    std::ostringstream oss;
    // Convert tm to UTC
#ifdef _WIN32
    struct tm tmStruct;
    gmtime_s(&tmStruct, &time);
    // Uncomment the next line and remove the following three if 
    // using g++ 5 or greater
    // oss << std::put_time( &tmStruct, "%Y%m%d-%H:%M:%S" );
    char buf[ 64 + 1 ];
    strftime(buf, 64, "%Y-%m-%dT%H:%M:%S", &tmStruct);
    oss << buf;
#else
    struct tm* tmStruct;
    tmStruct = gmtime( &time );
    // Uncomment the next line and remove the following three if 
    // using g++ 5 or greater
    //oss << std::put_time( tmStruct, "%Y%m%d-%H:%M:%S" );
    char buf[ 64 + 1 ];
    strftime(buf, 64, "%Y-%m-%dT%H:%M:%S", tmStruct);
    oss << buf;
#endif

    oss << "." << std::setfill( '0' ) << std::setw( 6 ) << microseconds;
    oss << "Z";
    std::string narrow = oss.str();
    return narrow;
}
} // anonymous namespace


mgo::Logger::Logger(const std::string& filename)
{
    m_log.open( filename, std::ios::app );
    if (! m_log )
    {
        throw std::runtime_error(
            "Could not open file " + filename + " for appending" );
    }
}

mgo::Logger::~Logger()
{
    m_log.close();
}

void mgo::Logger::Log(
    std::string const &message,
    char const *function,
    char const *file,
    int line
    )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_log << NowAsString() << "|"
          << function      << "|"
          << file          << "|"
          << line          << "|"
          << message << std::endl;
}

mgo::Logger* mgo::g_logger{ nullptr };
