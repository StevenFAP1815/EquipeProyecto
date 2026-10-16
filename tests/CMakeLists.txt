# Tests unitarios con Catch2.
#
# Este target es separado de delivery_sim. Catch2 solo se linkea aquí,
# no al binario de producción. Cumple la restricción del proyecto:
# el binario delivery_sim solo usa STL + OpenCV + JSON.

find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(delivery_tests
    test_geobounds.cpp
    test_config_loader.cpp
)

target_include_directories(delivery_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(delivery_tests PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
)

# Compilamos los .cpp de producción directamente en el target de tests
# para no obligar a construir una librería separada todavía.
target_sources(delivery_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/GeoBounds.cpp
    ${CMAKE_SOURCE_DIR}/src/infra/ConfigLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/concurrency/ShutdownToken.cpp
)

# Registrar los tests con CTest.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(delivery_tests)

# Los tests también deben heredar los flags de warnings.
if(MSVC)
    target_compile_options(delivery_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(delivery_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_definitions(delivery_tests PRIVATE
    TESTS_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)