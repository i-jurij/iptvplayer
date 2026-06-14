#include "Channel.h"
#include "MainFrame.h"
#include "EventIDs.h"

#include <string>
#include <wx/msgdlg.h>

void MainFrame::onChannelSelected(const Channel &ch, size_t, const wxRect &) {
  PlayChannel(ch);
}

void MainFrame::PlayChannel(const Channel &ch) {
  if (!m_videoPanel)
    return;

  // 1) Запускаем воспроизведение
  m_videoPanel->PlayChannel(ch);

  // 2) Переключаемся на Video
  CallAfter([this, ch]() {
    showPanel(m_videoPanel);

    // активируем кнопку Video
    if (m_btnVideo) {
      m_btnVideo->SetValue(true);

      wxCommandEvent evt(wxEVT_TOGGLEBUTTON, m_btnVideo->GetId());
      evt.SetEventObject(m_btnVideo);
      wxPostEvent(m_btnVideo, evt);
    }
  });
}
