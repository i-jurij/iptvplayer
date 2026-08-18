#include "Channel.h"
#include "MainFrame.h"

#include <wx/msgdlg.h>

void MainFrame::onFavoriteSelected(const Channel &ch, size_t, const wxRect &) {
  if (!m_videoPanel)
    return;
  m_videoPanel->SetChannelSourceTab(m_favoritesPageIdx);
  m_videoPanel->SetIsChannelPlaying(false);
  m_videoPanel->SetIsFavoritePlaying(true);

  m_epgPendingPanel = m_epgFavorites;
  m_epgPendingChannel = ch;
  m_epgDebounceTimer.StartOnce(300);
}
