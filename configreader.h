#pragma once

#include <string>
#include <unordered_map>

namespace mgo
{

class IConfigReader
{
public:
    virtual std::string read(
        const std::string& key,
        const std::string& defaultValue = ""
        ) const = 0;
    virtual unsigned long readLong(
        const std::string& key,
        unsigned long defaultValue = 0
        ) const = 0;
    virtual double readDouble(
        const std::string& key,
        double defaultValue = 0.0 ) const = 0;
    virtual bool readBool(
        const std::string& key,
        bool defaultValue ) const = 0;
};

// Note, default parameters are statically bound, but I'm leaving them
// all on the derived classes for reference. They should never need to change.

class MockConfigReader final : public IConfigReader
{
    std::string read(
        const std::string&,
        const std::string& defaultValue = ""
        ) const override
    {
        return defaultValue;
    }
    unsigned long readLong(
        const std::string&,
        unsigned long defaultValue = 0
        ) const override
    {
        return defaultValue;
    }
    double readDouble(
        const std::string&,
        double defaultValue = 0.0 ) const override
    {
        return defaultValue;
    }
    bool readBool(
        const std::string&,
        bool defaultValue ) const override
    {
        return defaultValue;
    }
};

class ConfigReader final : public IConfigReader
{
public:
    ConfigReader(const std::string& sConfigFileName);
    ~ConfigReader(){}
    std::string read(
        const std::string& key,
        const std::string& defaultValue = ""
        ) const override;
    unsigned long readLong(
        const std::string& key,
        unsigned long defaultValue = 0
        ) const override;
    double readDouble(
        const std::string& key,
        double defaultValue = 0.0 ) const override;
    bool readBool(
        const std::string& key,
        bool defaultValue ) const override;
private:
    std::unordered_map<std::string, std::string> m_map;
    // Lazy caches for bool, double, and long types.
    // We could use a map of variants, but as we only
    // support three types, it's simple enough like this.
    mutable std::unordered_map<std::string, bool>   m_mapBool;
    mutable std::unordered_map<std::string, double> m_mapDouble;
    mutable std::unordered_map<std::string, long>   m_mapLong;
};

} // namespace mgo

