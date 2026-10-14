cmake_minimum_required(VERSION 3.14)
project(delivery_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Asegurar que los binarios se generen ordenados en build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Biblioteca JSON (nlohmann) ---
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  )
  FetchContent_MakeAvailable(json)
endif()

find_package(Threads REQUIRED)

# --- OpenCV ---
# En MSYS2 UCRT64, find_package encuentra la configuración exacta exportada por OpenCV 5/4
find_package(OpenCV REQUIRED)

# --- Pruebas de visualizacion ---
# NOTA: Agrega src/MapLoader.cpp si ahí está implementado MapLoader::loadFromFile
add_executable(validar_visualizacion_mapa 
    tests/validar_visualizacion_mapa.cpp
    # src/MapLoader.cpp  <-- Descomenta si tienes un archivo .cpp para MapLoader
)

target_include_directories(validar_visualizacion_mapa PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(validar_visualizacion_mapa PRIVATE -Wall -Wextra)

# Enlace moderno usando el target importado de OpenCV
target_link_libraries(validar_visualizacion_mapa PRIVATE 
    Threads::Threads 
    nlohmann_json::nlohmann_json 
    ${OpenCV_LIBS}
)