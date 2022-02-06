cmake_minimum_required (VERSION 3.14)
project(libechidna)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

include(FetchContent)

# BEGIN echidna
add_subdirectory(src)


if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  add_subdirectory(tests)
  # enable testing in parent
  enable_testing()
endif()
# END echidna
