# CMake Windows defaults module

include_guard(GLOBAL)

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

include(buildspec)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  # ALLUSERSPROFILE uses backslashes, which break the generated cmake_install.cmake.
  file(TO_CMAKE_PATH "$ENV{ALLUSERSPROFILE}" _all_users_profile)
  set(
    CMAKE_INSTALL_PREFIX
    "${_all_users_profile}/obs-studio/plugins"
    CACHE STRING
    "Default plugin installation directory"
    FORCE
  )
endif()
