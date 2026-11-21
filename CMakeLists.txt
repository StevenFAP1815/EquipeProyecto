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

option(ENABLE_ASAN "Build with AddressSanitizer and LeakSanitizer" OFF)
if(ENABLE_ASAN)
    if(ENABLE_TSAN)
        message(FATAL_ERROR "ENABLE_ASAN y ENABLE_TSAN no se pueden usar a la vez.")
    endif()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
                        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
endif()

option(ENABLE_VIEWER "Build the OpenCV window" ON)
option(USE_CUDA "Compile the .cu sources with nvcc for an NVIDIA GPU" OFF)

if(USE_CUDA)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 75)
    endif()
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    find_package(CUDAToolkit REQUIRED)
endif()

find_package(Threads REQUIRED)
if(ENABLE_VIEWER)
    find_package(OpenCV REQUIRED COMPONENTS core highgui imgproc imgcodecs)
endif()

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

set(DELIVERY_GPU_SOURCES
    src/gpu/DeviceInfo.cu
    src/gpu/EventTimer.cu
    src/gpu/FloydWarshall.cu
    src/gpu/CandidateFilter.cu
    src/gpu/EtaBatch.cu
    src/gpu/ArgminReduction.cu
    src/gpu/DeliveryHistogram.cu
    src/gpu/RadixSort.cu
    src/gpu/GroupByKey.cu
    src/gpu/SegmentMerge.cu
    src/gpu/DemandHeatmap.cu
    src/gpu/CoverageField.cu
    src/gpu/ResponseMap.cu
    src/gpu/RouteArrivals.cu
)

add_library(delivery_gpu STATIC ${DELIVERY_GPU_SOURCES})
target_include_directories(delivery_gpu PUBLIC include)
target_link_libraries(delivery_gpu PUBLIC Threads::Threads)

if(USE_CUDA)
    target_link_libraries(delivery_gpu PUBLIC CUDA::cudart)
    target_compile_definitions(delivery_gpu PUBLIC DELIVERY_USE_CUDA)
else()
    set_source_files_properties(${DELIVERY_GPU_SOURCES} PROPERTIES LANGUAGE CXX)
    if(NOT MSVC)
        target_compile_options(delivery_gpu PRIVATE -x c++)
    endif()
endif()

add_library(delivery_core STATIC
    src/core/GeoBounds.cpp
    src/core/PathFinder.cpp
    src/core/StreetNetwork.cpp
    src/core/CsrGraph.cpp
    src/core/TravelTable.cpp
    src/core/PlanarProjection.cpp
    src/core/CandidateFilter.cpp
    src/core/GridBinning.cpp
    src/infra/ConfigLoader.cpp
    src/infra/EventLog.cpp
    src/concurrency/ShutdownToken.cpp
    src/concurrency/ChangeSignal.cpp
    src/concurrency/PickupDesk.cpp
    src/concurrency/OrderBook.cpp
    src/concurrency/GpuWorker.cpp
    src/sim/SimClock.cpp
    src/sim/OrderGenerator.cpp
    src/sim/Courier.cpp
    src/sim/Fleet.cpp
    src/sim/Dispatcher.cpp
    src/bench/BenchRunner.cpp
    src/bench/Applications.cpp
    src/viz/FrameStats.cpp
)
target_include_directories(delivery_core PUBLIC include)
target_link_libraries(delivery_core PUBLIC
    delivery_gpu
    nlohmann_json::nlohmann_json
    Threads::Threads
)

add_executable(delivery_sim
    src/main.cpp
)
target_link_libraries(delivery_sim PRIVATE
    delivery_core
)

if(ENABLE_VIEWER)
    target_sources(delivery_sim PRIVATE src/viz/Renderer.cpp)
    target_link_libraries(delivery_sim PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(delivery_sim PRIVATE DELIVERY_HAS_VIEWER)
endif()

if(ENABLE_TSAN)
    target_sources(delivery_sim PRIVATE src/analysis/tsan_default_suppressions.cpp)
endif()

enable_testing()
add_subdirectory(tests)
