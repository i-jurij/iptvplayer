#include "Channel.h"
#include "LogControl.h"
#include "MainFrame.h"

#include <wx/msgdlg.h>

void MainFrame::onChannelSelected(const Channel &ch, size_t, const wxRect &) {
  m_lastSelectedChannel = ch;
  m_hasLastSelectedChannel = true;
  
  if (!m_videoPanel)
    return;

  m_videoPanel->m_playbackSource = m_channelsPage;

  m_epgPendingPanel = m_epgChannels;
  m_epgPendingChannel = ch;
  m_epgDebounceTimer.StartOnce(300);
}
