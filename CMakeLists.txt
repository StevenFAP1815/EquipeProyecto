cmake_minimum_required(VERSION 3.16)
project(equipetrol_delivery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "ENABLE_TSAN no está soportado con MSVC. Use GCC o Clang en Linux/WSL2.")
    endif()

    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=thread")
    check_cxx_source_compiles("int main(){return 0;}" TSAN_SUPPORTED)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)

    if(NOT TSAN_SUPPORTED)
        message(FATAL_ERROR
            "ENABLE_TSAN=ON pero el compilador actual no incluye libtsan "
            "(caso típico: MSYS2/MinGW en Windows). "
            "Use esta opción en Linux o WSL2.")
    endif()

    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core highgui imgproc imgcodecs)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(delivery_core STATIC
    src/core/GeoBounds.cpp
    src/core/PathFinder.cpp
    src/core/StreetNetwork.cpp
    src/infra/ConfigLoader.cpp
    src/concurrency/ShutdownToken.cpp
    src/concurrency/ChangeSignal.cpp
    src/concurrency/OrderBook.cpp
    src/sim/SimClock.cpp
    src/sim/OrderGenerator.cpp
    src/sim/Courier.cpp
    src/sim/Fleet.cpp
    src/sim/Dispatcher.cpp
)
target_include_directories(delivery_core PUBLIC include)
target_link_libraries(delivery_core PUBLIC
    nlohmann_json::nlohmann_json
    Threads::Threads
)

add_executable(delivery_sim
    src/main.cpp
    src/viz/Renderer.cpp
)
target_link_libraries(delivery_sim PRIVATE
    delivery_core
    ${OpenCV_LIBS}
)

enable_testing()
add_subdirectory(tests)
