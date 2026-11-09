#pragma once

#include <delivery/infra/Config.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace delivery::infra {
class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& what) : std::runtime_error(what) {}
};

class ConfigFileError : public ConfigError {
public:
    explicit ConfigFileError(const std::string& what) : ConfigError(what) {}
};

class ConfigJsonError : public ConfigError {
public:
    explicit ConfigJsonError(const std::string& what) : ConfigError(what) {}
};

class ConfigSchemaError : public ConfigError {
public:
    explicit ConfigSchemaError(const std::string& what) : ConfigError(what) {}
};

class ConfigReferenceError : public ConfigError {
public:
    explicit ConfigReferenceError(const std::string& what) : ConfigError(what) {}
};

Config loadConfigFromFile(const std::filesystem::path& path);

Config parseConfig(const nlohmann::json& j);
}
