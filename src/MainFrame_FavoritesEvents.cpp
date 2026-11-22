#include "Channel.h"
#include "MainFrame.h"

#include <wx/msgdlg.h>

void MainFrame::onFavoriteSelected(const Channel &ch, size_t, const wxRect &) {
  m_lastSelectedFavorite = ch;
  m_hasLastSelectedFavorite = true;

  if (!m_videoPanel)
    return;

  m_videoPanel->m_playbackSource = m_favoritesPanel;

  m_epgPendingPanel = m_epgFavorites;
  m_epgPendingChannel = ch;
  m_epgDebounceTimer.StartOnce(300);
}
