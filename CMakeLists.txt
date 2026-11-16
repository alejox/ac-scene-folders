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
  PRIVATE
    src/plugin-main.cpp
    src/scene-groups-dock.cpp
    src/scene-groups-dock.hpp
    src/groups-api.cpp
    src/groups-api.hpp
    src/update-check.cpp
    src/update-check.hpp
    src/net-get.hpp
)

# Update check over HTTPS with the system network stack: OBS doesn't ship Qt's TLS plugins.
if(OS_WINDOWS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/net-get-win.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE winhttp)
elseif(OS_MACOS)
  enable_language(OBJCXX)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/net-get-mac.mm)
  set_source_files_properties(src/net-get-mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>")
else()
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/net-get-stub.cpp)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
