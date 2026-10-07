# SPDX-License-Identifier: GPL-2.0-or-later
# Standalone build of the chat and overlay tests (no OBS or Qt needed):
#   cmake -S tests -B build_tests
#   cmake --build build_tests --config RelWithDebInfo
#   ctest --test-dir build_tests -C RelWithDebInfo --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(tandem-tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TANDEM_BUILD_TESTS ON CACHE BOOL "" FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/../src/chat/chat.cmake)
add_subdirectory(overlay)
