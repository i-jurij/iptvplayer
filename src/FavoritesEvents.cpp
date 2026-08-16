#include "Channel.h"
#include "MainFrame.h"

#include <wx/msgdlg.h>

void MainFrame::onFavoriteSelected(const Channel &ch, size_t, const wxRect &) {
  if (!m_videoPanel)
    return;

  m_videoPanel->SetChannelSourceTab(m_favoritesPageIdx);
  m_videoPanel->SetIsChannelPlaying(false);
  m_videoPanel->SetIsFavoritePlaying(true);
  PlayChannel(ch);

  if (m_epgFavorites) {
    m_epgFavorites->SetChannel(ch);
  }
}
