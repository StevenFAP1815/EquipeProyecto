#pragma once

#include <delivery/infra/Config.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <nlohmann/json_fwd.hpp>

// Forward declaration para no incluir <nlohmann/json> en el header.
// Regla de A1.8: solo ConfigLoader.cpp incluye la libreria JSON.

namespace delivery::infra {

// ---------------------------------------------------------------------------
// Jerarquia de errores de configuracion (DD-008).
//
// Todas heredan de ConfigError, que hereda de std::runtime_error, para que
// main pueda capturar cualquier error de configuracion con un solo catch.
// Los tests pueden capturar el tipo especifico para verificar el motivo.
// ---------------------------------------------------------------------------

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& what) : std::runtime_error(what) {}
};

// El archivo no se puede abrir o leer.
class ConfigFileError : public ConfigError {
public:
    explicit ConfigFileError(const std::string& what) : ConfigError(what) {}
};

// El archivo existe pero el JSON esta mal formado.
class ConfigJsonError : public ConfigError {
public:
    explicit ConfigJsonError(const std::string& what) : ConfigError(what) {}
};

// El JSON es valido pero le falta una propiedad obligatoria, tiene un tipo
// incorrecto, o un valor fuera de rango.
class ConfigSchemaError : public ConfigError {
public:
    explicit ConfigSchemaError(const std::string& what) : ConfigError(what) {}
};

// El JSON es estructuralmente valido pero hace referencia a una entidad
// inexistente (por ejemplo, una calle apunta a un nodo que no existe),
// o viola unicidad de IDs.
class ConfigReferenceError : public ConfigError {
public:
    explicit ConfigReferenceError(const std::string& what) : ConfigError(what) {}
};

// ---------------------------------------------------------------------------
// API publica.
// ---------------------------------------------------------------------------

// Lee, parsea y valida un archivo de configuracion.
// Lanza ConfigFileError, ConfigJsonError, ConfigSchemaError o
// ConfigReferenceError segun corresponda.
Config loadConfigFromFile(const std::filesystem::path& path);

// Parseo y validacion puros (sin I/O). Util para tests que no tocan disco.
// Lanza ConfigJsonError, ConfigSchemaError o ConfigReferenceError.
Config parseConfig(const nlohmann::json& j);

}  // namespace delivery::infra