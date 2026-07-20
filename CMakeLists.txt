cmake_minimum_required(VERSION 3.16)
project(iptvplayer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# ------------------------
# Local wxWidgets install
# ------------------------
set(LOCAL_WX_INSTALL "${CMAKE_SOURCE_DIR}/third_party/wx/install" CACHE PATH "Local wxWidgets install dir")
list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_WX_INSTALL}")

find_package(wxWidgets CONFIG REQUIRED COMPONENTS
    core base aui xrc xml net adv gl
    HINTS "${LOCAL_WX_INSTALL}"
    NO_DEFAULT_PATH
)

message(STATUS "wxWidgets version: ${wxWidgets_VERSION}")
message(STATUS "wxWidgets dir: ${wxWidgets_DIR}")

file(TO_CMAKE_PATH "${wxWidgets_DIR}" _wx_dir_norm)
file(TO_CMAKE_PATH "${LOCAL_WX_INSTALL}" _local_norm)
string(FIND "${_wx_dir_norm}" "${_local_norm}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR "wxWidgets found outside local install: ${wxWidgets_DIR}")
endif()

# ------------------------
# Aggregate wx_all target
# ------------------------
set(_wx_components core base aui xrc xml net adv gl)
add_library(wx_all INTERFACE)

foreach(_comp IN LISTS _wx_components)
    set(_target "wx::${_comp}")
    if(TARGET ${_target})
        target_link_libraries(wx_all INTERFACE ${_target})
    endif()
endforeach()

# wxGLCanvas static lib
set(WX_GL_LIB "${LOCAL_WX_INSTALL}/lib/libwx_gtk3u_gl-3.3.a")
if(NOT EXISTS "${WX_GL_LIB}")
    message(FATAL_ERROR "wxGLCanvas library not found: ${WX_GL_LIB}")
endif()
target_link_libraries(wx_all INTERFACE "${WX_GL_LIB}")

# Wayland EGL for wxGLCanvas
find_library(WAYLAND_EGL_LIB NAMES wayland-egl libwayland-egl PATHS /usr/lib /usr/lib64 "${LOCAL_WX_INSTALL}/lib" NO_DEFAULT_PATH)
if(WAYLAND_EGL_LIB)
    target_link_libraries(wx_all INTERFACE ${WAYLAND_EGL_LIB})
endif()

# ------------------------
# CURL
# ------------------------
find_package(CURL REQUIRED)
if(TARGET CURL::libcurl)
    set(_CURL_TARGET CURL::libcurl)
else()
    set(_CURL_TARGET "")
endif()

# ------------------------
# Player backend (mpv logic, OpenGL — all in src/player)
# ------------------------
add_subdirectory(src/player)

# ------------------------
# Main application sources
# ------------------------
set(SOURCES
    src/main.cpp
    src/Application.cpp
    src/DialogsPL.cpp
    src/ConfigManager.cpp
    src/GUIManager.cpp
    src/MainFrame.cpp
    src/MainFrame_PlaylistLogic.cpp
    src/MainFrame_UI.cpp
    src/MainFrame_FiltersUI.cpp
    src/MainFrame_Filters.cpp
    src/MainFrame_ChannelsViewUI.cpp
    src/MainFrame_PlaylistUI.cpp
    src/MainFrame_FavoritesUI.cpp
    src/MainFrame_Menu.cpp
    src/VideoPanel.cpp
    src/VP_actions.cpp
    src/VP_events.cpp
    src/VP_tempplaylist.cpp
    src/MainFrame_Events.cpp
    src/MainFrame_PlaylistEvents.cpp
    src/BaseChannelList.cpp
    src/ChannelList.cpp
    src/ChannelDataModel.cpp
    src/LogoCache_Core.cpp
    src/LogoCache_Load.cpp
    src/LogoCache_Rescale.cpp
    src/CardsBase_core.cpp
    src/CardsBase_render.cpp
    src/CardsBase_cache.cpp
    src/CardsBase_events.cpp
    src/CardsBase_utils.cpp
    src/ChannelCards.cpp
    src/ChannelEvents.cpp
    src/Channel.cpp
    src/FavoritesCards.cpp
    src/FavoritesList.cpp
    src/FavoritesManager.cpp
    src/FavoritesEvents.cpp
    src/Playlist.cpp
    src/PlaylistManager.cpp
    src/M3UParser.cpp
    src/ThreadUtils.cpp
    src/TypeAheadSearch.cpp
    src/UpdateAllThread.cpp
    src/UpdateOneThread.cpp
    src/Utils.cpp
    src/SettingsDialog.cpp
    src/star_filled_png.cpp
    src/star_outline_png.cpp
    src/IconManager.cpp
    src/IconManager_Utils.cpp
    src/BackendFactory.cpp
    src/iptvorg/IPTVOrgMetadataManager.cpp
    src/iptvorg/AddIPTVPlaylistDialog.cpp
)

# ------------------------
# Executable
# ------------------------
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    wx_all
    player_backend_mpv
)

if(_CURL_TARGET)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${_CURL_TARGET})
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CURL_LIBRARIES})
endif()

# ------------------------
# Icons
# ------------------------
set(ICONS_DIR ${CMAKE_SOURCE_DIR}/icons)

add_custom_command(TARGET iptvplayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${ICONS_DIR}
            $<TARGET_FILE_DIR:iptvplayer>/icons
)

install(TARGETS iptvplayer DESTINATION bin)
install(DIRECTORY ${ICONS_DIR}/ DESTINATION share/iptvplayer/icons)

set(DATADIR "${CMAKE_INSTALL_PREFIX}/share")
target_compile_definitions(${PROJECT_NAME} PRIVATE DATADIR=\"${DATADIR}\")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
