# SPDX-License-Identifier: GPL-2.0-or-later
# Tandem chat core: providers, parsers and the event hub. No Qt and no libobs, so it can be
# built and tested on its own. Include after project():  include(src/chat/chat.cmake)

include_guard(GLOBAL)

option(TANDEM_BUILD_TESTS "Build the Tandem chat tests and the live probe tool" OFF)

set(_tandem_chat_dir "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(_tandem_root "${_tandem_chat_dir}/../.." ABSOLUTE)

find_package(CURL REQUIRED)

add_library(tandem-chat-core STATIC)
target_sources(
  tandem-chat-core
  PRIVATE
    "${_tandem_chat_dir}/backoff.h"
    "${_tandem_chat_dir}/chat-hub.cpp"
    "${_tandem_chat_dir}/chat-hub.h"
    "${_tandem_chat_dir}/chat-types.h"
    "${_tandem_chat_dir}/chat-util.cpp"
    "${_tandem_chat_dir}/chat-util.h"
    "${_tandem_chat_dir}/kick-provider.cpp"
    "${_tandem_chat_dir}/net.cpp"
    "${_tandem_chat_dir}/net.h"
    "${_tandem_chat_dir}/parse-kick.cpp"
    "${_tandem_chat_dir}/parse-kick.h"
    "${_tandem_chat_dir}/parse-twitch.cpp"
    "${_tandem_chat_dir}/parse-twitch.h"
    "${_tandem_chat_dir}/parse-youtube.cpp"
    "${_tandem_chat_dir}/parse-youtube.h"
    "${_tandem_chat_dir}/provider-base.cpp"
    "${_tandem_chat_dir}/provider-base.h"
    "${_tandem_chat_dir}/twitch-provider.cpp"
    "${_tandem_chat_dir}/youtube-provider.cpp"
)
target_include_directories(tandem-chat-core PUBLIC "${_tandem_chat_dir}" "${_tandem_root}/dep/nlohmann-json")
target_compile_features(tandem-chat-core PUBLIC cxx_std_20)
target_link_libraries(tandem-chat-core PUBLIC CURL::libcurl)
set_target_properties(tandem-chat-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(PROJECT_VERSION)
  target_compile_definitions(tandem-chat-core PRIVATE TANDEM_CHAT_VERSION="${PROJECT_VERSION}")
endif()

if(WIN32)
  target_compile_definitions(tandem-chat-core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(tandem-chat-core PUBLIC ws2_32)
endif()

# MSVC and clang-cl both take MSVC-style flags.
if(MSVC)
  target_compile_options(tandem-chat-core PRIVATE /utf-8 /W4)
else()
  target_compile_options(tandem-chat-core PRIVATE -Wall -Wextra)
endif()

if(TANDEM_BUILD_TESTS)
  enable_testing()

  file(GLOB _tandem_test_sources CONFIGURE_DEPENDS "${_tandem_root}/tests/test-*.cpp")
  add_executable(tandem-chat-tests ${_tandem_test_sources})
  target_link_libraries(tandem-chat-tests PRIVATE tandem-chat-core)
  target_compile_definitions(tandem-chat-tests PRIVATE TANDEM_TEST_FIXTURES="${_tandem_root}/tests/fixtures")
  if(MSVC)
    target_compile_options(tandem-chat-tests PRIVATE /utf-8 /W4)
  endif()
  add_test(NAME tandem-chat-tests COMMAND tandem-chat-tests)

  add_executable(tandem-chat-probe "${_tandem_root}/tests/probe.cpp")
  target_link_libraries(tandem-chat-probe PRIVATE tandem-chat-core)
  if(MSVC)
    target_compile_options(tandem-chat-probe PRIVATE /utf-8 /W4)
    # The probe reads TANDEM_YT_KEY with plain getenv().
    target_compile_definitions(tandem-chat-probe PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  if(WIN32 AND TARGET CURL::libcurl_shared)
    foreach(_tandem_exe IN ITEMS tandem-chat-tests tandem-chat-probe)
      add_custom_command(
        TARGET ${_tandem_exe}
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:CURL::libcurl_shared>" "$<TARGET_FILE_DIR:${_tandem_exe}>"
        VERBATIM
      )
    endforeach()
  endif()
endif()
