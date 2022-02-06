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

file(GLOB_RECURSE ECHIDNA_TEST_SRC ${PROJECT_SOURCE_DIR}/tests/*.cpp)
add_executable(echidnatest ${ECHIDNA_TEST_SRC} ${ECHIDNA_SRC})
target_include_directories(echidnatest PUBLIC ../include)
target_link_libraries(echidnatest gtest_main echidna)

# Load tests...
gtest_discover_tests(echidnatest)
