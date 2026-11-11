add_library(player_xtream OBJECT
    XtreamUrl.cpp
    XtreamUrl.h
    AddXtreamPlaylistDialog.cpp
    AddXtreamPlaylistDialog.h
    XtreamInfo.cpp
    XtreamInfo.h
    PlaylistInfoDialog.cpp
    PlaylistInfoDialog.h
    XtreamDatabase.cpp
    XtreamDatabase.h
    XtreamApiClient.cpp
    XtreamApiClient.h
    PosterCards.cpp
    PosterCards.h
    VodInfoPanel.cpp
    VodInfoPanel.h
    SeriesPanel.h
)

target_include_directories(player_xtream PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(player_xtream PRIVATE
    wx_all
    wxsqlite3
)

if(_CURL_TARGET)
    target_link_libraries(player_xtream PRIVATE ${_CURL_TARGET})
else()
    target_link_libraries(player_xtream PRIVATE ${CURL_LIBRARIES})
endif()