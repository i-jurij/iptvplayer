#include "EventIDs.h"
#include "IPlayerBackend.h"
#include "MainFrame.h"
#include "PlayerController.h"
#include "VideoPanel.h"

#include <wx/menu.h>
#include <wx/msgdlg.h>

static int NewMenuId() { return wxWindow::NewControlId(); }

void MainFrame::ShowMainMenu(const wxPoint &pos) {
  if (m_videoPanel)
    m_videoPanel->SetTabActive(false);

  wxMenu menu;

  // ---- Submenu "Video" ----
  wxMenu *videoMenu = new wxMenu;

  // Zoom
  wxMenu *zoomMenu = new wxMenu;
  {
    std::vector<std::pair<wxString, double>> zoomValues = {
        {"25%", 0.25},  {"50%", 0.5},  {"75%", 0.75}, {"100%", 0.0},
        {"125%", 1.25}, {"150%", 1.5}, {"200%", 2.0}};
    for (const auto &item : zoomValues) {
      int id = NewMenuId();
      zoomMenu->Append(id, item.first);
      zoomMenu->Bind(
          wxEVT_MENU,
          [this, value = item.second](wxCommandEvent &) {
            if (m_videoPanel && m_videoPanel->m_playerController)
              m_videoPanel->m_playerController->SetVideoZoom(value);
          },
          id);
    }
    zoomMenu->AppendSeparator();
    int idReset = NewMenuId();
    zoomMenu->Append(idReset, "Reset");
    zoomMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoZoom(0.0);
        },
        idReset);
  }
  videoMenu->AppendSubMenu(zoomMenu, "Zoom");

  // Aspect Ratio
  wxMenu *aspectMenu = new wxMenu;
  {
    std::vector<std::pair<wxString, wxString>> aspectValues = {
        {"16:9", "16:9"},     {"4:3", "4:3"},      {"21:9", "21:9"},
        {"16:10", "16:10"},   {"5:4", "5:4"},      {"1:1", "1:1"},
        {"2.35:1", "2.35:1"}, {"1.85:1", "1.85:1"}};
    for (const auto &item : aspectValues) {
      int id = NewMenuId();
      aspectMenu->Append(id, item.first);
      std::string aspectStr = item.second.ToStdString();
      aspectMenu->Bind(
          wxEVT_MENU,
          [this, aspectStr](wxCommandEvent &) {
            if (m_videoPanel && m_videoPanel->m_playerController)
              m_videoPanel->m_playerController->SetVideoAspect(aspectStr);
          },
          id);
    }
    aspectMenu->AppendSeparator();
    int idAuto = NewMenuId();
    aspectMenu->Append(idAuto, "Auto");
    aspectMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoAspect("-1");
        },
        idAuto);
    int idResetAsp = NewMenuId();
    aspectMenu->Append(idResetAsp, "Reset");
    aspectMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoAspect("-1");
        },
        idResetAsp);
  }
  videoMenu->AppendSubMenu(aspectMenu, "Aspect Ratio");

  // Rotate
  wxMenu *rotateMenu = new wxMenu;
  {
    int id0 = NewMenuId();
    rotateMenu->Append(id0, "0°");
    rotateMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoRotate(0);
        },
        id0);
    int id90 = NewMenuId();
    rotateMenu->Append(id90, "90°");
    rotateMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoRotate(90);
        },
        id90);
    int id180 = NewMenuId();
    rotateMenu->Append(id180, "180°");
    rotateMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoRotate(180);
        },
        id180);
    int id270 = NewMenuId();
    rotateMenu->Append(id270, "270°");
    rotateMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoRotate(270);
        },
        id270);
    rotateMenu->AppendSeparator();
    int idResetRot = NewMenuId();
    rotateMenu->Append(idResetRot, "Reset");
    rotateMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoRotate(0);
        },
        idResetRot);
  }
  videoMenu->AppendSubMenu(rotateMenu, "Rotate");

  // Mirror / Filter
  wxMenu *mirrorMenu = new wxMenu;
  {
    int idMirror = NewMenuId();
    mirrorMenu->Append(idMirror, "Toggle Mirror");
    mirrorMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->ToggleVideoMirror();
        },
        idMirror);
    int idResetFilters = NewMenuId();
    mirrorMenu->Append(idResetFilters, "Reset All Filters");
    mirrorMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->ResetVideoFilters();
        },
        idResetFilters);
  }
  videoMenu->AppendSubMenu(mirrorMenu, "Mirror / Filters");

  menu.AppendSubMenu(videoMenu, "Video");

  // ---- Submenu "Audio" ----
  wxMenu *audioMenu = new wxMenu;

  // ---- Track ----
  wxMenu *trackMenu = new wxMenu;
  {
    int currentAudio = -1;
    std::vector<std::pair<int, wxString>> tracks;
    if (m_videoPanel && m_videoPanel->m_playerController) {
      tracks = m_videoPanel->m_playerController->GetAudioTracks();
      currentAudio = m_videoPanel->m_playerController->GetCurrentAudioTrack();
    }
    if (tracks.empty()) {
      trackMenu->Append(NewMenuId(), "(no audio tracks)")->Enable(false);
    } else {
      for (const auto &[id, label] : tracks) {
        int menuId = NewMenuId();
        wxString itemLabel = label;
        if (id == currentAudio) {
          itemLabel = wxString::Format("✓ %s", label);
        }
        trackMenu->Append(menuId, itemLabel);
        trackMenu->Bind(
            wxEVT_MENU,
            [this, id](wxCommandEvent &) {
              if (m_videoPanel && m_videoPanel->m_playerController)
                m_videoPanel->m_playerController->SetAudioTrack(id);
            },
            menuId);
      }
    }
  }
  audioMenu->AppendSubMenu(trackMenu, "Track");

  // ---- Delay ----
  wxMenu *delayMenu = new wxMenu;

  // Пункт с текущим значением (неактивный, только для информации)
  double currentDelay = 0.0;
  if (m_videoPanel && m_videoPanel->m_playerController) {
    currentDelay = m_videoPanel->m_playerController->GetAudioDelay();
  }
  wxString currentLabel =
      wxString::Format("Current delay: %.2f s", currentDelay);
  int idCurrent = NewMenuId();
  delayMenu->Append(idCurrent, currentLabel);
  delayMenu->Enable(idCurrent, false); // делаем неактивным

  delayMenu->AppendSeparator();

  // Предустановленные значения задержки (в секундах)
  std::vector<double> delayValues = {-1.0, -0.5, -0.3, -0.2, -0.1, 0.0,
                                     0.1,  0.2,  0.3,  0.5,  1.0};
  for (double val : delayValues) {
    int id = NewMenuId();
    wxString label;
    if (val == 0.0)
      label = "Sync (0s)";
    else if (val > 0)
      label = wxString::Format("+%.1fs", val);
    else
      label = wxString::Format("%.1fs", val);
    delayMenu->Append(id, label);
    delayMenu->Bind(
        wxEVT_MENU,
        [this, val](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController) {
            m_videoPanel->m_playerController->SetAudioDelay(val);
          }
        },
        id);
  }
  delayMenu->AppendSeparator();
  // Пункт для ручного ввода
  int idCustom = NewMenuId();
  delayMenu->Append(idCustom, "Custom...");
  delayMenu->Bind(
      wxEVT_MENU,
      [this](wxCommandEvent &) {
        if (m_videoPanel && m_videoPanel->m_playerController) {
          wxTextEntryDialog dlg(
              this, "Enter audio delay in seconds (e.g., -0.5 or 0.3):",
              "Custom Audio Delay", "0.0");
          if (dlg.ShowModal() == wxID_OK) {
            wxString input = dlg.GetValue();
            double val;
            if (input.ToDouble(&val)) {
              m_videoPanel->m_playerController->SetAudioDelay(val);
            } else {
              wxMessageBox("Invalid number. Please enter a numeric value.",
                           "Error", wxOK | wxICON_ERROR, this);
            }
          }
        }
      },
      idCustom);

  audioMenu->AppendSubMenu(delayMenu, "Delay");

  menu.AppendSubMenu(audioMenu, "Audio");

  // ---- Submenu "Subtitles" ----
  wxMenu *subMenu = new wxMenu;
  {
    int currentSub = -1;
    std::vector<std::pair<int, wxString>> tracks;
    if (m_videoPanel && m_videoPanel->m_playerController) {
      tracks = m_videoPanel->m_playerController->GetSubtitleTracks();
      currentSub = m_videoPanel->m_playerController->GetCurrentSubtitleTrack();
    }
    if (tracks.empty()) {
      subMenu->Append(NewMenuId(), "(no subtitle tracks)")->Enable(false);
    } else {
      for (const auto &[id, label] : tracks) {
        int menuId = NewMenuId();
        wxString itemLabel = label;
        if (id == currentSub) {
          itemLabel = wxString::Format("✓ %s", label);
        }
        subMenu->Append(menuId, itemLabel);
        subMenu->Bind(
            wxEVT_MENU,
            [this, id](wxCommandEvent &) {
              if (m_videoPanel && m_videoPanel->m_playerController)
                m_videoPanel->m_playerController->SetSubtitleTrack(id);
            },
            menuId);
      }
    }
  }
  menu.AppendSubMenu(subMenu, "Subtitles");
  
  menu.AppendSeparator();
  menu.Append(ID_MENU_SETTINGS, "Settings");
  menu.Append(ID_MENU_ABOUT, "About");
  menu.Append(ID_MENU_EXIT, "Quit");

  // ---- Показ меню ----
  if (pos == wxDefaultPosition) {
    PopupMenu(&menu);
  } else {
    // Явно указанная позиция (например, для правого клика, если хотим точное место)
    PopupMenu(&menu, pos);
  }
}