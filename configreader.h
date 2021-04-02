#pragma once

#include <string>
#include <unordered_map>

namespace mgo
{

class ConfigReader{
public:
    ConfigReader(const std::string& sConfigFileName);
    ~ConfigReader(){}
    std::string read(
        const std::string& key,
        const std::string& defaultValue = ""
        ) const;
    unsigned long readLong(
        const std::string& key,
        unsigned long defaultValue = 0
        );
    double readDouble(
        const std::string& key,
        double defaultValue = 0.0 );
    bool readBool(
        const std::string& key,
        bool defaultValue );
    std::string operator[](const std::string& key) const;
private:
    std::unordered_map<std::string, std::string> m_map;
    // Lazy caches for bool, double, and long types.
    // We could use a map of variants, but as we only
    // support three types, it's simple enough like this.
    std::unordered_map<std::string, bool>   m_mapBool;
    std::unordered_map<std::string, double> m_mapDouble;
    std::unordered_map<std::string, long>   m_mapLong;
};

} // namespace mgo

