
# Optionally glob, but only for CMake 3.12 or later:
file(GLOB HEADER_LIST CONFIGURE_DEPENDS "${libechidna_SOURCE_DIR}/include/echidna/*.hpp")
#set(HEADER_LIST "${libechidna_SOURCE_DIR}/include/echidna/string-util.hpp")

# Make an automatic library - will be static or dynamic based on user setting
add_library(echidna string-util.cpp buf.cpp file.cpp config.cpp tcp.cpp udp.cpp mem.cpp event_hlpr.cpp event_mgr.cpp store.cpp pipeline.cpp websocket.cpp openssl_mgr.cpp timer.cpp ${HEADER_LIST})
target_compile_options(echidna PRIVATE -Werror -Wall -Wextra)
target_link_libraries(echidna PRIVATE numa)

# We need this directory, and users of our library will need it too
target_include_directories(echidna PUBLIC ../include)

# All users of this library will need at least C++11
target_compile_features(echidna PUBLIC cxx_std_11)

# IDEs should put the headers in a nice place
source_group(
  TREE "${PROJECT_SOURCE_DIR}/include"
  PREFIX "Header Files"
  FILES ${HEADER_LIST})
