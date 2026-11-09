#pragma once

#include <wx/dialog.h>

class Playlist;

class PlaylistInfoDialog : public wxDialog {
public:
  PlaylistInfoDialog(wxWindow *parent, const Playlist &playlist);

  static bool ShouldShowExpiryWarning(const Playlist &playlist);

private:
  void BuildUi(const Playlist &playlist);
};
