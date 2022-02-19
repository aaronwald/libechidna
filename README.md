# libechidna

Layout from https://cliutils.gitlab.io/modern-cmake/chapters/basics/structure.html

apt-get install libnuma-dev libyaml-dev libssl-dev
export CC=/usr/bin/clang-12
export CXX=/usr/bin/clang++-12

cmake -GNinja -S . -B build
cmake --build build
cmake --build build --target test
