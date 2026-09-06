cmake_minimum_required(VERSION 3.16)


# ---- Чтение версии из файла VERSION ----
if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION LIMIT_COUNT 1)
    string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
else()
    set(PROJECT_VERSION "0.0.0")
endif()

# ---- Получение Git-идентификатора (если доступен) ----
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE GIT_COMMIT
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE GIT_DESCRIBE
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
else()
    set(GIT_COMMIT "")
    set(GIT_DESCRIBE "")
endif()

# ---- Формируем полную версию для отображения и для имён файлов ----
if(GIT_COMMIT)
    set(PROJECT_VERSION_FULL "${PROJECT_VERSION}+g${GIT_COMMIT}")
    set(PROJECT_VERSION_FILE "${PROJECT_VERSION}-${GIT_COMMIT}")
else()
    set(PROJECT_VERSION_FULL "${PROJECT_VERSION}")
    set(PROJECT_VERSION_FILE "${PROJECT_VERSION}")
endif()

# ---- Устанавливаем проект и генерируем version.h ----
project(iptvplayer VERSION ${PROJECT_VERSION})
configure_file(${CMAKE_SOURCE_DIR}/version.h.in ${CMAKE_BINARY_DIR}/version.h @ONLY)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------
# Local wxWidgets install
# ------------------------
set(LOCAL_WX_INSTALL "${CMAKE_SOURCE_DIR}/third_party/wx/install" CACHE PATH "Local wxWidgets install dir")
list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_WX_INSTALL}")

find_package(wxWidgets CONFIG REQUIRED COMPONENTS
    core base aui adv gl
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
# Local wxSQLite3 install (надёжный поиск с проверкой)
# ------------------------
set(LOCAL_WXSQLITE3_INSTALL "${CMAKE_SOURCE_DIR}/third_party/wxsqlite3/install" CACHE PATH "Local wxSQLite3 install dir")
set(WXSQLITE3_INCLUDE_DIR "${LOCAL_WXSQLITE3_INSTALL}/include")

# Поиск статической библиотеки – GLOB с проверкой единственности
file(GLOB WXSQLITE3_LIBRARIES "${LOCAL_WXSQLITE3_INSTALL}/lib/libwxcode*.a")
list(LENGTH WXSQLITE3_LIBRARIES _count)
if(NOT _count EQUAL 1)
    message(FATAL_ERROR 
        "Ожидается ровно один файл библиотеки wxSQLite3, но найдено ${_count}:\n"
        "  ${WXSQLITE3_LIBRARIES}\n"
        "Удалите лишние или переустановите wxSQLite3."
    )
endif()
set(WXSQLITE3_LIBRARY "${WXSQLITE3_LIBRARIES}")
message(STATUS "wxSQLite3 library: ${WXSQLITE3_LIBRARY}")

# Импортная цель
add_library(wxsqlite3 STATIC IMPORTED)
set_target_properties(wxsqlite3 PROPERTIES
    IMPORTED_LOCATION "${WXSQLITE3_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${WXSQLITE3_INCLUDE_DIR}"
)

# ------------------------
# Aggregate wx_all target
# ------------------------
set(_wx_components core base aui adv gl)
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
# ZLIB (для gzip распаковки)
# ------------------------
find_package(ZLIB REQUIRED)

find_package(EXPAT REQUIRED)

# ------------------------
# Player backend
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
    src/MainFrame_Events.cpp
    src/MainFrame_PlaylistEvents.cpp
    src/MainFrame_ChannelEvents.cpp
    src/MainFrame_FavoritesEvents.cpp
    src/VideoPanel.cpp
    src/VP_actions.cpp
    src/VP_events.cpp
    src/VP_tempplaylist.cpp
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
    src/Channel.cpp
    src/FavoritesCards.cpp
    src/FavoritesList.cpp
    src/FavoritesManager.cpp
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
    src/epg/EPGManager.cpp
    src/epg/EPGParserExpat.cpp
    src/epg/EPGPanel.cpp
    src/epg/EpgSourceManagerPanel.cpp
    src/epg/AddEpgSourceDialog.cpp
    src/epg/EPGDatabase.cpp
    src/epg/HashUtils.cpp
    src/epg/ManualMappingDialog.cpp
)

# ------------------------
# Executable
# ------------------------
add_executable(${PROJECT_NAME} ${SOURCES})

# ------------------------
# Проверка наличия timegm (для EPG парсинга)
# ------------------------
include(CheckSymbolExists)
check_symbol_exists(timegm "time.h" HAVE_TIMEGM)
if(HAVE_TIMEGM)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_TIMEGM)
    message(STATUS "timegm found, using native implementation")
else()
    message(STATUS "timegm not found, using portable implementation")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${WXSQLITE3_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}   # для version.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    wx_all
    player_backend_mpv
    ZLIB::ZLIB
    wxsqlite3
    EXPAT::EXPAT
)

if(_CURL_TARGET)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${_CURL_TARGET})
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CURL_LIBRARIES})
endif()

# ------------------------
# Макрос для установки ресурсов
# ------------------------
function(install_resource FILE_NAME)
    set(FULL_PATH "${CMAKE_SOURCE_DIR}/resources/${FILE_NAME}")
    if(NOT EXISTS "${FULL_PATH}")
        message(WARNING "Ресурсный файл не найден: ${FULL_PATH}")
        return()
    endif()

    # Копирование в каталог сборки
    add_custom_command(TARGET iptvplayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FULL_PATH}"
            "$<TARGET_FILE_DIR:iptvplayer>/${FILE_NAME}"
        COMMENT "Copying ${FILE_NAME} to build directory"
    )

    # Установка в зависимости от платформы
    if(WIN32)
        install(FILES "${FULL_PATH}" DESTINATION .)
    elseif(APPLE)
        set(BUNDLE_RESOURCES_DIR "${CMAKE_BINARY_DIR}/iptvplayer.app/Contents/Resources")
        add_custom_command(TARGET iptvplayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FULL_PATH}"
                "${BUNDLE_RESOURCES_DIR}/${FILE_NAME}"
            COMMENT "Copying ${FILE_NAME} to macOS app bundle"
        )
        install(FILES "${FULL_PATH}"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/iptvplayer.app/Contents/Resources")
    else()
        install(FILES "${FULL_PATH}" DESTINATION share/iptvplayer)
    endif()
endfunction()

# ------------------------
# Установка ресурсов
# ------------------------
install_resource(epg_sources.json)
install_resource(matching_rules.json)
install_resource(channel_aliases.json)

# ---- Сохранение версий в install/ ----
if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    # Чистая версия уже в PROJECT_VERSION
    # Формируем файлы версий
    set(VERSION_CONTENT "${PROJECT_VERSION}")
    set(VERSION_FULL_CONTENT "${PROJECT_VERSION_FULL}")
    set(VERSION_FILE_CONTENT "${PROJECT_VERSION_FILE}")

    # Записываем в файлы внутри каталога сборки, а затем устанавливаем
    file(WRITE "${CMAKE_BINARY_DIR}/VERSION" "${VERSION_CONTENT}")
    file(WRITE "${CMAKE_BINARY_DIR}/VERSION_FULL" "${VERSION_FULL_CONTENT}")
    file(WRITE "${CMAKE_BINARY_DIR}/VERSION_FILE" "${VERSION_FILE_CONTENT}")

    # Устанавливаем их в install/
    install(FILES
        "${CMAKE_BINARY_DIR}/VERSION"
        "${CMAKE_BINARY_DIR}/VERSION_FULL"
        "${CMAKE_BINARY_DIR}/VERSION_FILE"
        DESTINATION .
    )
endif()

# ---- Установка appdata.xml в install/ (для DEB/RPM/AppImage) ----
set(APPDATA_FILE ${CMAKE_SOURCE_DIR}/resources/iptvplayer.appdata.xml)
if(EXISTS ${APPDATA_FILE})
    # Гарантированно создаём каталог и копируем файл в install/share/metainfo
    install(CODE "
        message(STATUS \"Installing appdata.xml to ${CMAKE_INSTALL_PREFIX}/share/metainfo\")
        file(MAKE_DIRECTORY \"${CMAKE_INSTALL_PREFIX}/share/metainfo\")
        file(COPY \"${APPDATA_FILE}\" DESTINATION \"${CMAKE_INSTALL_PREFIX}/share/metainfo\")
    ")
endif()

# ------------------------
# Иконки
# ------------------------
set(ICONS_DIR ${CMAKE_SOURCE_DIR}/icons)

add_custom_command(TARGET iptvplayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${ICONS_DIR}
            $<TARGET_FILE_DIR:iptvplayer>/icons
)

install(TARGETS iptvplayer DESTINATION bin)
install(DIRECTORY ${ICONS_DIR}/ DESTINATION share/iptvplayer/icons)

# ------------------------
# DATADIR для JSON-файлов
# ------------------------
set(DATADIR "${CMAKE_INSTALL_PREFIX}/share")
target_compile_definitions(${PROJECT_NAME} PRIVATE DATADIR=\"${DATADIR}\")

# ------------------------
# Компиляторные флаги
# ------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)

    # Оптимизация размера для релизных сборок
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections)
    endif()
endif()
