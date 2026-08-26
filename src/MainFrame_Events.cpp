#include "IconManager.h"
#include "MainFrame.h"
#include "SettingsDialog.h"
#include "UpdateAllThread.h"
#include "UpdateOneThread.h"
#include "Utils.h"

#include <wx/msgdlg.h>

void MainFrame::onSettings(wxCommandEvent &WXUNUSED(event)) {
  SettingsDialog dlg(this, getConfigManager());
  dlg.ShowModal();
}

void MainFrame::onQuit(wxCommandEvent &WXUNUSED(event)) {
  IconManager::Shutdown();
  m_closing = true;
  Close(true);
}

void MainFrame::onAbout(wxCommandEvent &WXUNUSED(event)) {
  showInfo(this,
           "IPTV Player v1.0.0\n\n"
           "Milestone 5: EPG Management\n\n"
           "Author: I-Jurij",
           "About IPTV Player");
}

void MainFrame::onToggleFavoritesView(wxCommandEvent &) {
  if (m_favViewToggle->GetValue()) {
    m_favViewBook->ChangeSelection(1); // Grid
    m_favViewToggle->SetLabel("List View");
  } else {
    m_favViewBook->ChangeSelection(0); // List
    m_favViewToggle->SetLabel("Grid View");
  }
}

void MainFrame::OnEpgProgress(const EpgProgressInfo &info) {
  LOG_DEBUG("MainFrame::OnEpgProgress: stage=%d, percent=%d, text=%s",
            static_cast<int>(info.stage), info.percent, info.stageText.c_str());

  bool isActive = (info.stage != EpgProgressStage::None &&
                   info.stage != EpgProgressStage::Done &&
                   info.stage != EpgProgressStage::Cancelled &&
                   info.stage != EpgProgressStage::Error);

  if (isActive) {
    // Показываем гейдж
    if (!m_gaugeTop->IsShown()) {
      m_gaugeTop->Show();
      m_gaugeTop->SetMinSize(wxSize(-1, FromDIP(20)));
      m_gaugeTop->SetRange(100);
      if (m_gaugeTop->GetParent()) {
        m_gaugeTop->GetParent()->Layout();
      }
      m_gaugeTop->Refresh();
    }

    // Обновляем гейдж
    if (info.percent >= 0) {
      int percent = info.percent;
      if (info.stage == EpgProgressStage::Downloading && info.totalBytes > 0) {
        percent =
            static_cast<int>((info.downloadedBytes / info.totalBytes) * 100);
      }
      m_gaugeTop->SetValue(percent);
    } else {
      m_gaugeTop->Pulse();
    }

    // Панель 0: краткое действие
    wxString brief;
    switch (info.stage) {
    case EpgProgressStage::Downloading:
      brief = "EPG: Downloading";
      break;
    case EpgProgressStage::Extracting:
      brief = "EPG: Extracting";
      break;
    case EpgProgressStage::Parsing:
      brief = "EPG: Parsing";
      break;
    case EpgProgressStage::Matching:
      brief = "EPG: Matching channels";
      break;
    default:
      brief = "EPG: " + wxString::FromUTF8(info.stageText);
      break;
    }
    SetStatusText(brief, 0);

    // Панель 1: детали
    wxString details = wxString::FromUTF8(info.stageText);
    if (info.stage == EpgProgressStage::Downloading && info.totalBytes > 0) {
      double downloadedMB = info.downloadedBytes / (1024.0 * 1024.0);
      double totalMB = info.totalBytes / (1024.0 * 1024.0);
      double speedKB = info.speedBytesPerSec / 1024.0;
      details += wxString::Format(" (%.1f MB / %.1f MB, %.1f KB/s)",
                                  downloadedMB, totalMB, speedKB);
    } else if (info.stage == EpgProgressStage::Matching) {
      details += wxString::Format(" (%d/%d)", info.matched, info.totalChannels);
    } else if (info.percent >= 0) {
      details += wxString::Format(" (%d%%)", info.percent);
    }
    SetStatusText(details, 1);
  } else {
    // Скрываем гейдж
    if (m_gaugeTop->IsShown()) {
      m_gaugeTop->Hide();
      m_gaugeTop->SetValue(0);
      if (m_gaugeTop->GetParent()) {
        m_gaugeTop->GetParent()->Layout();
      }
    }

    // Статус-бар завершения
    if (info.stage == EpgProgressStage::Done) {
      SetStatusText("EPG success", 0);
      wxString msg = wxString::Format(_("EPG updated: %d/%d channels matched"),
                                      info.matched, info.totalChannels);
      SetStatusText(msg, 1);
    } else if (info.stage == EpgProgressStage::Cancelled) {
      SetStatusText("EPG cansel", 0);
      SetStatusText("EPG update cancelled", 1);
    } else if (info.stage == EpgProgressStage::Error) {
      SetStatusText("EPG error", 0);
      SetStatusText(wxString::FromUTF8(info.errorMessage), 1);
    } else {
      SetStatusText("", 0);
      SetStatusText("", 1);
    }

    // Обновляем панели EPG (каналы и избранное)
    if (m_epgChannels && m_epgChannels->HasChannel()) {
      m_epgChannels->LoadProgramsForChannel(
          m_epgChannels->GetCurrentChannelId(),
          m_epgChannels->GetCurrentDate());
    }
    if (m_epgFavorites && m_epgFavorites->HasChannel()) {
      m_epgFavorites->LoadProgramsForChannel(
          m_epgFavorites->GetCurrentChannelId(),
          m_epgFavorites->GetCurrentDate());
    }
  }
}

void MainFrame::OnEpgDebounceTimer(wxTimerEvent &) {
  if (m_epgPendingPanel) {
    m_epgPendingPanel->SetChannel(m_epgPendingChannel);
    m_epgPendingPanel = nullptr;
  }
}

void MainFrame::OnEpgToggle(wxCommandEvent &) {
  if (m_videoPanel)
    m_videoPanel->SetTabActive(false);
  ToggleHeaderGroup(m_btnEpg);
  m_notebook->SetSelection(m_epgPageIdx);
}

void MainFrame::OnGlobalCharHook(wxKeyEvent &evt) {
  int key = evt.GetKeyCode();

  // ESC – выход из fullscreen (если он активен)
  if (key == WXK_ESCAPE) {
    // TypeAheadSearch перехватывает ESC в своих виджетах и не передаёт дальше,
    // поэтому здесь ESC не дойдёт, если фокус в поиске.
    if (m_videoPanel && m_videoPanel->IsFullscreen()) {
      m_videoPanel->ToggleFullscreen();
      evt.Skip(false);
      return;
    }
  }
  // F/F – переключение fullscreen
  else if (key == 'f' || key == 'F') {
    if (m_videoPanel) {
      bool isFullscreen = m_videoPanel->IsFullscreen();
      bool isVideoPage = IsVideoPageActive();

      // Если fullscreen уже включён – выключаем всегда (с любой страницы)
      if (isFullscreen) {
        m_videoPanel->ToggleFullscreen();
        evt.Skip(false);
        return;
      }
      // Если fullscreen выключен – включаем только на Video
      else if (isVideoPage) {
        m_videoPanel->ToggleFullscreen();
        evt.Skip(false);
        return;
      }
    }
  }

  evt.Skip();
}

