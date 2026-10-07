# SPDX-License-Identifier: GPL-2.0-or-later
# Standalone build of the overlay tests and demo:
#   cmake -S tests/overlay -B build_overlay_tests
#   cmake --build build_overlay_tests --config Debug
cmake_minimum_required(VERSION 3.20)
project(tandem-overlay-tests CXX)

set(TANDEM_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

if(NOT TARGET tandem-chat-core)
	include(${TANDEM_ROOT}/src/chat/chat.cmake)
endif()

include(${TANDEM_ROOT}/src/overlay/overlay.cmake)

foreach(t test-overlay overlay-demo)
	add_executable(${t} ${CMAKE_CURRENT_LIST_DIR}/${t}.cpp)
	target_link_libraries(${t} PRIVATE tandem-overlay tandem-chat-core)
	target_include_directories(${t} PRIVATE ${TANDEM_ROOT}/src/chat ${TANDEM_ROOT}/dep/nlohmann-json)
	if(WIN32)
		target_link_libraries(${t} PRIVATE ws2_32)
	endif()
	if(MSVC)
		target_compile_options(${t} PRIVATE /utf-8 /W4)
	endif()
	if(WIN32 AND TARGET CURL::libcurl_shared)
		add_custom_command(TARGET ${t} POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:CURL::libcurl_shared>" "$<TARGET_FILE_DIR:${t}>"
			VERBATIM)
	endif()
endforeach()

enable_testing()
add_test(NAME test-overlay COMMAND test-overlay)
