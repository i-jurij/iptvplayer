#include "Channel.h"
#include "LogControl.h"
#include "MainFrame.h"

#include <wx/msgdlg.h>

void MainFrame::onChannelSelected(const Channel &ch, size_t, const wxRect &) {
  if (!m_videoPanel)
    return;
  m_videoPanel->SetChannelSourceTab(m_channelsPageIdx);
  m_videoPanel->SetIsChannelPlaying(true);
  m_videoPanel->SetIsFavoritePlaying(false);

  m_epgPendingPanel = m_epgChannels;
  m_epgPendingChannel = ch;
  m_epgDebounceTimer.StartOnce(300);
}
