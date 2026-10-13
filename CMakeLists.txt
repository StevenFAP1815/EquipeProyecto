cmake_minimum_required(VERSION 3.11)
project(delivery_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Biblioteca JSON (nlohmann) ---
find_package(nlohmann_json 3.2 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_GetProperties(json)
  if(NOT json_POPULATED)
    FetchContent_Populate(json)
    add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()
endif()

find_package(Threads REQUIRED)

# --- SOLO COMPILAMOS LA PRUEBA VISUAL POR AHORA ---
find_package(OpenCV REQUIRED)

add_executable(validar_visualizacion_mapa tests/validar_visualizacion_mapa.cpp)
target_compile_options(validar_visualizacion_mapa PRIVATE -Wall -Wextra)
target_link_libraries(validar_visualizacion_mapa PRIVATE Threads::Threads nlohmann_json::nlohmann_json ${OpenCV_LIBS})