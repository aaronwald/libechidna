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
  GIT_SHALLOW    ON
)

FetchContent_GetProperties(spdlog)
if (NOT spdlog_POPULATED)
  FetchContent_Populate(spdlog)
  add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR})
endif()
# END spdlog

find_package(OpenSSL REQUIRED)

file(GLOB_RECURSE ECHIDNA_TEST_SRC ${PROJECT_SOURCE_DIR}/tests/testcases-*.cpp)
set(echidnatest_flags -DSPDLOG_DEBUG_ON=1 -DSPDLOG_TRACE_ON=1)
add_executable(echidnatest ${ECHIDNA_TEST_SRC} ${ECHIDNA_SRC} )
target_include_directories(echidnatest PUBLIC ../include)
target_link_libraries(echidnatest gtest_main echidna yaml numa spdlog::spdlog OpenSSL::SSL OpenSSL::Crypto )

add_executable(echidnassltest ${PROJECT_SOURCE_DIR}/tests/test-ssl.cpp ${ECHIDNA_SRC} )
target_include_directories(echidnassltest PUBLIC ../include)
target_link_libraries(echidnassltest gtest_main echidna yaml numa spdlog::spdlog OpenSSL::SSL OpenSSL::Crypto )

# Load tests...
gtest_discover_tests(echidnatest)
