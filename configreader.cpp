#include "configreader.h"

#include <algorithm>
#include <cctype>
#include <locale>
#include <fstream>

namespace
{

void ltrim( std::string &s )
{
    s.erase( s.begin(), std::find_if( s.begin(), s.end(), []( int ch )
        {
            return !std::isspace(ch);
        }
        )
    );
}

void rtrim( std::string &s )
{
    s.erase( std::find_if( s.rbegin(), s.rend(), []( int ch )
        {
            return !std::isspace(ch);
        }
        ).base(), s.end()
    );
}

void trim( std::string& s )
{
    ltrim(s);
    rtrim(s);
}

} // end anonymous namespace

namespace mgo
{

ConfigReader::ConfigReader( const std::string& cfgfile )
{
    // Open the config file
    std::ifstream ifs;
    ifs.open( cfgfile.c_str(), std::ios::in );
    if ( !ifs )
    {
        throw std::runtime_error( "Could not open " + cfgfile );
    }
    std::string line;
    size_t n;
    while ( getline( ifs, line ) )
    {
        trim( line );
        if ( line != "" && line[ 0 ] != '#' )
        {
            n = line.find( "=" );
            if ( n > 0 )
            {
                std::string key = line.substr( 0, n );
                trim( key );
                std::transform( key.begin(), key.end(), key.begin(), ::toupper );
                std::string value = line.substr( n + 1 );
                trim( value );
                m_map[ key ] = value;
            }
        }
    }
    ifs.close();
}

std::string ConfigReader::read(
    const std::string& key,
    const std::string& defaultValue
    ) const
{
    std::string upperKey = key;
    std::string value;
    trim( upperKey );
    std::transform( upperKey.begin(), upperKey.end(), upperKey.begin(), ::toupper );
    auto it = m_map.find( upperKey );
    if ( it == m_map.end() )
    {
        value = defaultValue;
    }
    else
    {
        value = it->second;
        if ( value.empty() )
            value = defaultValue;
    }
    // Finally - if the value contains the string ${...} then we
    // should substitute its contents for another variable from the
    // config file.
    size_t pos;
    while ( ( pos = value.find( "${" ) ) != std::string::npos )
    {
        size_t endPos = value.find( "}", pos );
        if ( endPos == std::string::npos )
        {
            break; // no terminating brace so return string as is
        }
        std::string key = value.substr( pos + 2, ( endPos - pos ) - 2 );
        value = value.substr( 0, pos ) + read( key, "" ) +
                value.substr( endPos + 1 );
    }
    return value;
}

unsigned long ConfigReader::readLong(
    const std::string& key,
    unsigned long defaultValue
    ) const
{
    auto it = m_mapLong.find( key );
    if( it != m_mapLong.end() )
    {
        return it->second;
    }
    long rc = std::stoul( read( key, std::to_string( defaultValue ) ) );
    m_mapLong.insert( { key, rc } );
    return rc;
}

double ConfigReader::readDouble(
    const std::string& key,
    double defaultValue
    ) const
{
    auto it = m_mapDouble.find( key );
    if( it != m_mapDouble.end() )
    {
        return it->second;
    }
    double rc = std::stod( read( key, std::to_string( defaultValue ) ) );
    m_mapDouble.insert( { key, rc } );
    return rc;
}

bool ConfigReader::readBool(
    const std::string& key,
    bool defaultBoolValue
    ) const
{
    auto it = m_mapBool.find( key );
    if( it != m_mapBool.end() )
    {
        return it->second;
    }
    std::string defaultValue;
    if ( defaultBoolValue == true )
    {
        defaultValue = "Y";
    }
    else
    {
        defaultValue = "N";
    }
    std::string value = read( key, defaultValue );
    std::transform( value.begin(), value.end(), value.begin(), ::toupper );
    bool rc;
    if ( value[ 0 ] == 'Y' || value[ 0 ] == 'T' )
    {
        rc = true;
    }
    else
    {
        rc = false;
    }
    m_mapBool.insert( { key, rc } );
    return rc;
}

} // namespace mgo

