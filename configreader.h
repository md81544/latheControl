#pragma once

#include <map>
#include <string>

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
        ) const;
    double readDouble(
        const std::string& key,
        double defaultValue = 0.0 ) const;
    bool readBool(
        const std::string& key,
        bool defaultValue ) const;
    std::string operator[](const std::string& key) const;
private:
    std::map<std::string, std::string> m_map;
};

} // namespace mgo

