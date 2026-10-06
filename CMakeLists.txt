cmake_minimum_required(VERSION 3.11)
project(delivery_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ENABLE_TSAN "Compilar con ThreadSanitizer" OFF)

# --- Biblioteca JSON (nlohmann): la usa si esta instalada, si no la descarga ---
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

add_executable(delivery_sim src/main.cpp)
target_compile_options(delivery_sim PRIVATE -Wall -Wextra)
target_link_libraries(delivery_sim PRIVATE Threads::Threads nlohmann_json::nlohmann_json)

if(ENABLE_TSAN)
  target_compile_options(delivery_sim PRIVATE -fsanitize=thread -g)
  target_link_options(delivery_sim PRIVATE -fsanitize=thread)
endif()

# --- OpenCV (para la ventana, paso posterior). Si esta, se enlaza. ---
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
  target_include_directories(delivery_sim PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(delivery_sim PRIVATE ${OpenCV_LIBS})
endif()
