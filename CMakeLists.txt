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
  # The vertical canvas preview needs a private Qt header for Wayland on Qt < 6.9.
  if(OS_LINUX AND Qt6_VERSION VERSION_LESS "6.9.0")
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)
    # Distros can ship the CMake target without the headers (no qt6-base-private-dev).
    set(_qt_qpa_header FALSE)
    if(TARGET Qt6::GuiPrivate)
      get_target_property(_qt_private_dirs Qt6::GuiPrivate INTERFACE_INCLUDE_DIRECTORIES)
      foreach(_dir IN LISTS _qt_private_dirs)
        if(EXISTS "${_dir}/qpa/qplatformnativeinterface.h")
          set(_qt_qpa_header TRUE)
        endif()
      endforeach()
    endif()
    if(_qt_qpa_header)
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::GuiPrivate)
    else()
      message(WARNING "qt6-base-private-dev not found: vertical canvas preview will not work on Wayland")
      target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MEKETREVE_NO_QT_PRIVATE)
    endif()
  endif()
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/tools/bass-shake.c
    src/tools/vertical/audio-wrapper-source.c
    src/tools/vertical/audio-wrapper-source.h
    src/tools/vertical/config-dialog.cpp
    src/tools/vertical/config-dialog.hpp
    src/tools/vertical/display-helpers.hpp
    src/tools/vertical/hotkey-edit.cpp
    src/tools/vertical/hotkey-edit.hpp
    src/tools/vertical/multi-canvas-source.c
    src/tools/vertical/multi-canvas-source.h
    src/tools/vertical/name-dialog.cpp
    src/tools/vertical/name-dialog.hpp
    src/tools/vertical/obs-websocket-api.h
    src/tools/vertical/projector.cpp
    src/tools/vertical/projector.hpp
    src/tools/vertical/qt-display.cpp
    src/tools/vertical/qt-display.hpp
    src/tools/vertical/scenes-dock.cpp
    src/tools/vertical/scenes-dock.hpp
    src/tools/vertical/source-tree.cpp
    src/tools/vertical/source-tree.hpp
    src/tools/vertical/sources-dock.cpp
    src/tools/vertical/sources-dock.hpp
    src/tools/vertical/transitions-dock.cpp
    src/tools/vertical/transitions-dock.hpp
    src/tools/vertical/version.h
    src/tools/vertical/vertical-canvas.cpp
    src/tools/vertical/vertical-canvas.hpp
    src/tools/vertical/vertical.h
    src/tools/vertical/vertical.qrc
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
