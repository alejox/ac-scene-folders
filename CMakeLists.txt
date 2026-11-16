cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.cpp src/scene-groups-dock.cpp src/scene-groups-dock.hpp src/groups-api.cpp src/groups-api.hpp
)

# Self-update (ported from AC Goal Bar): checks GitHub releases, downloads and verifies the
# package, and installs it with a script that runs after OBS closes.
include(FetchContent)
FetchContent_Declare(
  nlohmann_json_header
  URL
    https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
    https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
  URL_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json_header)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src/updater)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE "${nlohmann_json_header_SOURCE_DIR}")
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/updater/core/cancel_token.cpp
    src/updater/core/sha256.cpp
    src/updater/update/atomic_file.cpp
    src/updater/update/install_plan.cpp
    src/updater/update/staging.cpp
    src/updater/update/update_check.cpp
    src/updater/update/update_service.cpp
    src/updater/update/update_settings.cpp
    src/updater/update/update_settings_file.cpp
    src/updater/ui/update_controller.cpp
)

# HTTPS with the system network stack: OBS doesn't ship Qt's TLS plugins.
if(OS_WINDOWS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/updater/net/net-win.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE winhttp)
elseif(OS_MACOS)
  enable_language(OBJCXX)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/updater/net/net-mac.mm)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>" "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>"
  )
else()
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/updater/net/net-stub.cpp)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
