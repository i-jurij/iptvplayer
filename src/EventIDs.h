// EventIDs.h
#ifndef EVENTIDS_H
#define EVENTIDS_H

// NOTE: explicit numeric ranges to avoid collisions with wxID_ANY and
// wxID_HIGHEST. Adjust base values only if your project already uses these
// numbers.

//
// Playlist related IDs  Range 2000..2099
//
enum {
  ID_PLAYLIST_BASE = 2000,
  ID_ADD_PLAYLIST_FILE = ID_PLAYLIST_BASE + 1,
  ID_ADD_PLAYLIST_URL = ID_PLAYLIST_BASE + 2,
  ID_UPDATE_ALL_PLAYLISTS = ID_PLAYLIST_BASE + 3,
  ID_OPEN_PLAYLIST = ID_PLAYLIST_BASE + 4,
  ID_UPDATE_PLAYLIST = ID_PLAYLIST_BASE + 5,
  ID_EDIT_PLAYLIST = ID_PLAYLIST_BASE + 6,
  ID_REMOVE_PLAYLIST = ID_PLAYLIST_BASE + 7,
  ID_EXPORT_PLAYLIST = ID_PLAYLIST_BASE + 8,
  ID_PLAYLIST_LIST =   ID_PLAYLIST_BASE + 50,
};

//
// Playlist async / worker events  Range 3000..3099 (numeric IDs for worker
// commands if needed)
//
enum {
  ID_PLAYLIST_WORK_BASE = 3000,
  ID_ADD_FROM_URL_SUCCESS = ID_PLAYLIST_WORK_BASE + 1,
  ID_ADD_FROM_URL_ERROR = ID_PLAYLIST_WORK_BASE + 2
};

//
// Menu and global commands  Range 4000..4099
//
enum {
  ID_MENU_BASE = 4000,
  ID_MENU_SETTINGS = ID_MENU_BASE + 1,
  ID_MENU_ABOUT = ID_MENU_BASE + 2,
  ID_MENU_EXIT = ID_MENU_BASE + 3
};

//
// Channels view and toolbar  Range 5000..5099
//
enum {
  ID_CHANNELS_BASE = 5000,
  ID_VIEW_LIST = ID_CHANNELS_BASE + 1,
  ID_VIEW_GRID = ID_CHANNELS_BASE + 2,
  ID_CHANNEL_SELECT = ID_CHANNELS_BASE + 3,
  ID_SHOW_LOGO = ID_CHANNELS_BASE + 4
};

//
// Favorites related  Range 5100..5199
//
enum {
  ID_FAV_BASE = 5100,
  ID_FAV_VIEW_LIST = ID_FAV_BASE + 1,
  ID_FAV_VIEW_GRID = ID_FAV_BASE + 2,
  ID_FAV_TOGGLE = ID_FAV_BASE + 3
};

//
// UI helpers and timers  Range 5200..5299
//
enum { ID_UI_BASE = 5200, ID_PROGRESS_TIMER = ID_UI_BASE + 1 };

//
// VideoPanel related IDs  Range 6000..6099
//
enum { ID_VP_BASE = 6000, ID_VP_TEMP_PLAYLIST_LIST = ID_VP_BASE + 1 };

#endif // EVENTIDS_H
