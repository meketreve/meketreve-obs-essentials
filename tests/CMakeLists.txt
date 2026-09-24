# Developer tools: not part of the plugin, never built by CI.
# Configure with -DENABLE_DEV_TOOLS=ON (see tests/README.md).

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Test)

set(_chat "${CMAKE_SOURCE_DIR}/src/tools/unified-chat")

add_library(chat-core STATIC)
target_sources(
  chat-core
  PRIVATE
    ${_chat}/chat-connector.cpp
    ${_chat}/chat-connector.hpp
    ${_chat}/irc-message.cpp
    ${_chat}/irc-message.hpp
    ${_chat}/kick-chat.cpp
    ${_chat}/kick-chat.hpp
    ${_chat}/tiktok-chat.cpp
    ${_chat}/tiktok-chat.hpp
    ${_chat}/tiktok-proto.cpp
    ${_chat}/tiktok-proto.hpp
    ${_chat}/twitch-chat.cpp
    ${_chat}/twitch-chat.hpp
    ${_chat}/ws-client.cpp
    ${_chat}/ws-client.hpp
    ${_chat}/youtube-chat.cpp
    ${_chat}/youtube-chat.hpp
)
target_include_directories(chat-core PUBLIC ${_chat} ${CMAKE_SOURCE_DIR}/src/tools/config)
target_link_libraries(chat-core PUBLIC Qt6::Core Qt6::Network)
set_target_properties(chat-core PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)

add_executable(chat-probe chat-probe.cpp)
target_link_libraries(chat-probe PRIVATE chat-core)

set(_config "${CMAKE_SOURCE_DIR}/src/tools/config")

add_executable(
  dock-harness
  dock-harness.cpp
  ${_chat}/unified-chat-dock.cpp
  ${_chat}/unified-chat-dock.hpp
  ${_config}/config-codec.cpp
  ${_config}/config-share.cpp
)
target_link_libraries(
  dock-harness
  PRIVATE chat-core Qt6::Widgets OBS::libobs OBS::obs-frontend-api plugin-support
)
set_target_properties(dock-harness PROPERTIES AUTOMOC ON)
target_compile_definitions(dock-harness PRIVATE HARNESS_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

add_executable(test-chat-parsers test-chat-parsers.cpp)
target_link_libraries(test-chat-parsers PRIVATE chat-core Qt6::Test)
set_target_properties(test-chat-parsers PROPERTIES AUTOMOC ON)
add_test(NAME chat-parsers COMMAND test-chat-parsers)

add_executable(
  test-layout-store
  test-layout-store.cpp
  ${CMAKE_SOURCE_DIR}/src/tools/tabs/layout-store.cpp
  ${CMAKE_SOURCE_DIR}/src/tools/tabs/layout-store.hpp
)
target_include_directories(test-layout-store PRIVATE ${CMAKE_SOURCE_DIR}/src/tools/tabs)
target_link_libraries(test-layout-store PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test-layout-store PROPERTIES AUTOMOC ON)
add_test(NAME layout-store COMMAND test-layout-store)

add_executable(test-config-codec test-config-codec.cpp ${_config}/config-codec.cpp ${_config}/config-codec.hpp)
target_include_directories(test-config-codec PRIVATE ${_config})
target_compile_definitions(test-config-codec PRIVATE PRESETS_DIR="${CMAKE_SOURCE_DIR}/data/presets")
target_link_libraries(test-config-codec PRIVATE Qt6::Core Qt6::Test)
set_target_properties(test-config-codec PROPERTIES AUTOMOC ON)
add_test(NAME config-codec COMMAND test-config-codec)
