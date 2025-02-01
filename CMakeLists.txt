cmake_minimum_required (VERSION 3.14)
project(libechidna)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set_property(GLOBAL PROPERTY USE_FOLDERFOLDERS ON)
endif()

include(FetchContent)
find_package(Protobuf REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)


# BEGIN echidna
add_subdirectory(src)


if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  add_subdirectory(tests)
  # enable testing in parent
  enable_testing()
endif()
# END echidna
