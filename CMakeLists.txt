cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_DEV_TOOLS "Build local test harnesses and unit tests (not shipped)" OFF)

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
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
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
    src/plugin-main.c
    src/tools/bass-shake.c
    src/tools/voice-fx.c
    src/tools/config/config-codec.cpp
    src/tools/config/config-codec.hpp
    src/tools/config/config-share.cpp
    src/tools/config/config-share.hpp
    src/tools/outputs/output-store.cpp
    src/tools/outputs/output-store.hpp
    src/tools/outputs/outputs-dock.cpp
    src/tools/outputs/outputs-dock.hpp
    src/tools/tabs/layout-store.cpp
    src/tools/tabs/layout-store.hpp
    src/tools/tabs/tab-bar.cpp
    src/tools/tabs/tab-bar.hpp
    src/tools/unified-chat/chat-accounts.cpp
    src/tools/unified-chat/chat-accounts.hpp
    src/tools/unified-chat/chat-connector.cpp
    src/tools/unified-chat/chat-connector.hpp
    src/tools/unified-chat/irc-message.cpp
    src/tools/unified-chat/irc-message.hpp
    src/tools/unified-chat/kick-chat.cpp
    src/tools/unified-chat/kick-chat.hpp
    src/tools/unified-chat/oauth-util.cpp
    src/tools/unified-chat/oauth-util.hpp
    src/tools/unified-chat/tiktok-chat.cpp
    src/tools/unified-chat/tiktok-chat.hpp
    src/tools/unified-chat/tiktok-proto.cpp
    src/tools/unified-chat/tiktok-proto.hpp
    src/tools/unified-chat/twitch-chat.cpp
    src/tools/unified-chat/twitch-chat.hpp
    src/tools/unified-chat/unified-chat-dock.cpp
    src/tools/unified-chat/unified-chat-dock.hpp
    src/tools/unified-chat/ws-client.cpp
    src/tools/unified-chat/ws-client.hpp
    src/tools/unified-chat/youtube-chat.cpp
    src/tools/unified-chat/youtube-chat.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(ENABLE_DEV_TOOLS)
  enable_testing()
  add_subdirectory(tests)
endif()
