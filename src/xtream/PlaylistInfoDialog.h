#pragma once

#include <wx/dialog.h>

#include <functional>

class Playlist;

class PlaylistInfoDialog : public wxDialog {
public:
  PlaylistInfoDialog(wxWindow *parent, const Playlist &playlist);

  // Stage 7.2: callback при клике «Update source…».
  // Вызывается с (playlist, suggestedSource). MainFrame открывает
  // EditPlaylistDialog с prefill.
  using UpdateSourceCallback =
      std::function<void(const Playlist &, const wxString &)>;
  void SetUpdateSourceCallback(UpdateSourceCallback cb) {
    m_onUpdateSource = std::move(cb);
  }

  static bool ShouldShowExpiryWarning(const Playlist &playlist);

private:
  void BuildUi(const Playlist &playlist);
  UpdateSourceCallback m_onUpdateSource;
  const Playlist &m_playlistRef;
};
