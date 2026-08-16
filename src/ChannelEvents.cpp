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
  PlayChannel(ch);

  if (m_epgChannels) {
    m_epgChannels->SetChannel(ch);
  }
}

void MainFrame::PlayChannel(const Channel &ch) {
  if (!m_videoPanel)
    return;

  // Инкремент токена — предыдущие отложенные showPanel станут неактуальны
  uint64_t token = ++m_showPanelToken;

  // Запускаем воспроизведение
  m_videoPanel->PlayChannel(ch);

  // Планируем безопасное переключение: выполняем только если токен актуален
  CallAfter([this, token]() {
    // Проверка токена
    if (token != m_showPanelToken.load()) {
      LOG_DEBUG("PlayChannel::CallAfter: token stale, skipping showPanel");
      return;
    }

    // Проверяем состояние плеера
    if (!(m_videoPanel && m_videoPanel->m_playerController)) {
      LOG_DEBUG("PlayChannel::CallAfter: no videoPanel or playerController");
      return;
    }

    auto state = m_videoPanel->m_playerController->GetState();
    if (state != PlayerState::Playing) {
      LOG_DEBUG("PlayChannel::CallAfter: player not Playing (state=%d), "
                "skipping showPanel",
                (int)state);
      return;
    }

    // Показываем панель Video и синхронно обновляем UI
    showPanel(m_videoPanel);
    if (m_btnVideo) {
      m_btnVideo->SetValue(true);
      ToggleHeaderGroup(m_btnVideo);
    }

    // Форсируем обновление canvas и сообщаем backend о размере
    auto *area = m_videoPanel->GetVideoArea();
    if (area) {
      area->Show();
      area->Refresh();
      area->Update();
      area->SetFocus();

      int w = 0, h = 0;
      area->GetClientSize(&w, &h);
      if (w > 0 && h > 0 && m_videoPanel->m_playerController) {
        m_videoPanel->m_playerController->ResizeEmbeddedWindow(w, h);
      }
    }
  });
}
