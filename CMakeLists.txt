cmake_minimum_required (VERSION 3.14)
project(libechidna)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# BEGIN googletest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.11.0
)

include(GoogleTest)
FetchContent_MakeAvailable(googletest)
enable_testing()
# END googletest

# BEGIN spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.x
)

FetchContent_GetProperties(spdlog)
if (NOT spdlog_POPULATED)
  FetchContent_Populate(spdlog)
  add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR})
endif()
# END spdlog

# BEGIN echidna
include_directories(${PROJECT_SOURCE_DIR}/src/main)
file(GLOB_RECURSE echidna_SRC ${PROJECT_SOURCE_DIR}/src/main/*.cpp)
file(GLOB_RECURSE echidna_TEST_SRC ${PROJECT_SOURCE_DIR}/src/test/*.cpp)

add_library(echidna ${echidna_SRC})
target_link_libraries(echidna pthread spdlog::spdlog numa ssl crypto yaml)
# END echidna

# Load tests...
# gtest_discover_tests(echidnatest)
