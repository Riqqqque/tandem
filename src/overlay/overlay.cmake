# SPDX-License-Identifier: GPL-2.0-or-later
# Tandem - local chat overlay server (static library). Include after src/chat/chat.cmake,
# which defines tandem-chat-core.

add_library(tandem-overlay STATIC
	${CMAKE_CURRENT_LIST_DIR}/overlay-server.cpp
	${CMAKE_CURRENT_LIST_DIR}/overlay-server.h
	${CMAKE_CURRENT_LIST_DIR}/overlay-page.h)

target_compile_features(tandem-overlay PUBLIC cxx_std_20)
target_include_directories(tandem-overlay
	PUBLIC ${CMAKE_CURRENT_LIST_DIR}
	PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../chat ${CMAKE_CURRENT_LIST_DIR}/../../dep/nlohmann-json)
target_link_libraries(tandem-overlay PUBLIC tandem-chat-core)
set_target_properties(tandem-overlay PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
	target_link_libraries(tandem-overlay PRIVATE ws2_32)
	target_compile_definitions(tandem-overlay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(MSVC)
	target_compile_options(tandem-overlay PRIVATE /utf-8 /W4)
endif()
