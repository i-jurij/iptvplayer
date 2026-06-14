#pragma once
#include "Playlist.h"
#include <wx/clntdata.h>

// Обёртка для передачи Playlist через wxClientData
struct PlaylistClientData : public wxClientData {
  Playlist playlist;
  int index;

  PlaylistClientData(const Playlist &pl, int idx) : playlist(pl), index(idx) {}

  virtual ~PlaylistClientData() = default; // ✅ Critical fix
};