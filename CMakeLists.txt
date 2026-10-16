cmake_minimum_required(VERSION 3.16)
project(equipetrol_delivery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Dependencias ----
find_package(OpenCV REQUIRED COMPONENTS core highgui imgproc imgcodecs)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ---- Target ----
set(DELIVERY_SOURCES
    src/main.cpp
    src/core/GeoBounds.cpp
    src/infra/ConfigLoader.cpp
    src/concurrency/ShutdownToken.cpp
)
add_executable(delivery_sim ${DELIVERY_SOURCES})

target_include_directories(delivery_sim PRIVATE include src)
target_link_libraries(delivery_sim PRIVATE
    ${OpenCV_LIBS}
    nlohmann_json::nlohmann_json
)
# ---- Tests ----
enable_testing()
add_subdirectory(tests)

# ---- ThreadSanitizer (opcional) ----
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "ENABLE_TSAN no está soportado con MSVC. Use Clang o GCC en Linux/WSL2.")
    endif()

    # Detectar si el linker puede encontrar libtsan (solo en toolchains que la proveen)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=thread")
    check_cxx_source_compiles("int main(){return 0;}" TSAN_SUPPORTED)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)

    if(NOT TSAN_SUPPORTED)
        message(FATAL_ERROR
            "ENABLE_TSAN=ON pero el toolchain actual no provee libtsan "
            "(caso típico: MSYS2/MinGW en Windows). "
            "Ejecute esta configuración en Linux o WSL2.")
    endif()

    target_compile_options(delivery_sim PRIVATE -fsanitize=thread -g -O1)
    target_link_options(delivery_sim PRIVATE -fsanitize=thread)
endif()