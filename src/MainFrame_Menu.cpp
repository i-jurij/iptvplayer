#include "EventIDs.h"
#include "IPlayerBackend.h"
#include "MainFrame.h"
#include "PlayerController.h"
#include "VideoPanel.h"

#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/numdlg.h>
#include <wx/textdlg.h>

#include <cstdio>
#include <memory>

static int NewMenuId() { return wxWindow::NewControlId(); }

void MainFrame::ShowMainMenu(bool deferred) {
  fprintf(stderr, "[MARK] M1: ShowMainMenu enter deferred=%d\n", (int)deferred);
  fflush(stderr);

  auto menuPtr = std::make_shared<wxMenu>();
  wxMenu &menu = *menuPtr;

  auto *pc = (m_videoPanel && m_videoPanel->m_playerController)
                 ? m_videoPanel->m_playerController.get()
                 : nullptr;

  // Радио-подменю с блоком "Reset to default".
  auto addRadioGroup =
      [&](wxMenu *parent, const wxString &label, const char *prop,
          const std::vector<std::pair<wxString, const char *>> &items) {
        wxMenu *sub = new wxMenu;
        std::string current;
        if (pc)
          pc->GetPropertyString(prop, current);

        for (auto &item : items) {
          int id = NewMenuId();
          sub->AppendRadioItem(id, item.first);
          if (current == item.second)
            sub->Check(id, true);
          std::string val(item.second);
          sub->Bind(
              wxEVT_MENU,
              [this, prop, val](wxCommandEvent &) {
                if (m_videoPanel)
                  m_videoPanel->SetMpvPropertyAndPersist(prop, val);
              },
              id);
        }

        sub->AppendSeparator();
        int idReset = NewMenuId();
        sub->Append(idReset, _U("Reset to default"));
        std::string propCopy(prop);
        sub->Bind(
            wxEVT_MENU,
            [this, propCopy](wxCommandEvent &) {
              if (m_videoPanel)
                m_videoPanel->ResetMpvPropertyToDefault(propCopy.c_str());
            },
            idReset);

        parent->AppendSubMenu(sub, label);
      };

  // Флаговый переключатель с блоком "Reset to default".
  auto addFlagToggle = [&](wxMenu *parent, const wxString &label,
                           const char *prop, const char *onVal,
                           const char *offVal, bool defaultOn) {
    std::string current;
    bool on = defaultOn;
    if (pc && pc->GetPropertyString(prop, current))
      on = (current == onVal);

    int id = NewMenuId();
    parent->AppendCheckItem(id, label);
    parent->Check(id, on);
    parent->Bind(
        wxEVT_MENU,
        [this, prop, onVal, offVal](wxCommandEvent &evt) {
          if (!m_videoPanel)
            return;
          std::string val = evt.IsChecked() ? onVal : offVal;
          m_videoPanel->SetMpvPropertyAndPersist(prop, val);
        },
        id);
  };

  // ---- Submenu "Video" ----
  wxMenu *videoMenu = new wxMenu;

  // Zoom
  wxMenu *zoomMenu = new wxMenu;
  {
    double currentZoom = 0.0;
    if (pc)
      pc->GetVideoZoom(currentZoom);
    int currentPct = static_cast<int>(std::round(currentZoom * 100.0));
    if (currentZoom == 0.0)
      currentPct = 100;

    int idCurrentZoom = NewMenuId();
    zoomMenu->Append(idCurrentZoom,
                     wxString::Format(_U("Current: %d%%"), currentPct));
    zoomMenu->Enable(idCurrentZoom, false);
    zoomMenu->AppendSeparator();

    std::vector<std::pair<wxString, double>> zoomValues = {
        {_U("25%"), 0.25}, {_U("50%"), 0.5},   {_U("75%"), 0.75},
        {_U("100%"), 0.0}, {_U("125%"), 1.25}, {_U("150%"), 1.5},
        {_U("200%"), 2.0}};
    for (const auto &item : zoomValues) {
      int id = NewMenuId();
      zoomMenu->AppendRadioItem(id, item.first);
      double diff = std::abs(currentZoom - item.second);
      if (diff < 0.01)
        zoomMenu->Check(id, true);
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
    zoomMenu->Append(idReset, _U("Reset"));
    zoomMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoZoom(0.0);
        },
        idReset);
  }
  videoMenu->AppendSubMenu(zoomMenu, _U("Zoom"));

  // Aspect Ratio
  wxMenu *aspectMenu = new wxMenu;
  {
    std::string currentAspect;
    if (pc)
      pc->GetPropertyString("video-aspect-override", currentAspect);

    wxString currentLabel = _U("Current: Auto");
    if (!currentAspect.empty() && currentAspect != "original")
      currentLabel = _U("Current: ") + wxString::FromUTF8(currentAspect);
    int idCurAsp = NewMenuId();
    aspectMenu->Append(idCurAsp, currentLabel);
    aspectMenu->Enable(idCurAsp, false);
    aspectMenu->AppendSeparator();

    std::vector<std::pair<wxString, wxString>> aspectValues = {
        {_U("16:9"), "16:9"},     {_U("4:3"), "4:3"},      {_U("21:9"), "21:9"},
        {_U("16:10"), "16:10"},   {_U("5:4"), "5:4"},      {_U("1:1"), "1:1"},
        {_U("2.35:1"), "2.35:1"}, {_U("1.85:1"), "1.85:1"}};
    for (const auto &item : aspectValues) {
      int id = NewMenuId();
      aspectMenu->AppendRadioItem(id, item.first);
      std::string aspectStr = item.second.ToStdString();
      if (currentAspect == aspectStr)
        aspectMenu->Check(id, true);
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
    aspectMenu->Append(idAuto, _U("Auto"));
    aspectMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoAspect("original");
        },
        idAuto);
    int idResetAsp = NewMenuId();
    aspectMenu->Append(idResetAsp, _U("Reset"));
    aspectMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoAspect("original");
        },
        idResetAsp);
  }
  videoMenu->AppendSubMenu(aspectMenu, _U("Aspect Ratio"));

  // Rotate
  wxMenu *rotateMenu = new wxMenu;
  {
    int currentRotate = 0;
    if (pc)
      pc->GetVideoRotate(currentRotate);

    int idCurRot = NewMenuId();
    rotateMenu->Append(
        idCurRot, wxString::Format(_U("Current: %d\u00b0"), currentRotate));
    rotateMenu->Enable(idCurRot, false);
    rotateMenu->AppendSeparator();

    std::vector<int> rotations = {0, 90, 180, 270};
    for (int deg : rotations) {
      int id = NewMenuId();
      rotateMenu->AppendRadioItem(id, wxString::Format(_U("%d\u00b0"), deg));
      if (currentRotate == deg)
        rotateMenu->Check(id, true);
      rotateMenu->Bind(
          wxEVT_MENU,
          [this, deg](wxCommandEvent &) {
            if (m_videoPanel && m_videoPanel->m_playerController)
              m_videoPanel->m_playerController->SetVideoRotate(deg);
          },
          id);
    }
    rotateMenu->AppendSeparator();
    int idResetRot = NewMenuId();
    rotateMenu->Append(idResetRot, _U("Reset"));
    rotateMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->SetVideoRotate(0);
        },
        idResetRot);
  }
  videoMenu->AppendSubMenu(rotateMenu, _U("Rotate"));

  // Mirror / Filter
  wxMenu *mirrorMenu = new wxMenu;
  {
    std::string vfChain;
    if (pc)
      pc->GetPropertyString("vf", vfChain);
    auto vfHas = [&vfChain](const char *label) {
      return vfChain.find(label) != std::string::npos;
    };

    int idMirror = NewMenuId();
    mirrorMenu->AppendCheckItem(idMirror, _U("Toggle Mirror"));
    mirrorMenu->Check(idMirror, vfHas("@mirror"));
    mirrorMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->ToggleVideoMirror();
        },
        idMirror);

    int idFlipV = NewMenuId();
    mirrorMenu->AppendCheckItem(idFlipV, _U("Toggle Vertical Flip"));
    mirrorMenu->Check(idFlipV, vfHas("@vflip"));
    mirrorMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->ToggleVideoFlipVertical();
        },
        idFlipV);

    int idDeint = NewMenuId();
    mirrorMenu->AppendCheckItem(idDeint, _U("Toggle Deinterlace"));
    mirrorMenu->Check(idDeint, vfHas("@deint"));
    mirrorMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->ToggleVideoDeinterlace();
        },
        idDeint);

    int idSharp = NewMenuId();
    mirrorMenu->AppendCheckItem(idSharp, _U("Toggle Sharpen"));
    mirrorMenu->Check(idSharp, vfHas("@sharp"));
    mirrorMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->ToggleVideoSharpen();
        },
        idSharp);

    int idCrop = NewMenuId();
    mirrorMenu->Append(idCrop, _U("Crop\u2026"));
    mirrorMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (!m_videoPanel || !m_videoPanel->m_playerController)
            return;
          wxTextEntryDialog dlg(this,
                                _U("Crop parameters w:h:x:y, empty to reset:"),
                                _U("Video Crop"), "");
          if (dlg.ShowModal() != wxID_OK)
            return;
          wxString input = dlg.GetValue().Trim();
          if (input.IsEmpty()) {
            m_videoPanel->m_playerController->SendCommand("vf remove @crop");
          } else {
            m_videoPanel->m_playerController->SendCommand("vf set @crop:crop=" +
                                                          input.ToStdString());
          }
        },
        idCrop);

    int idResetFilters = NewMenuId();
    mirrorMenu->Append(idResetFilters, _U("Reset All Filters"));
    mirrorMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel && m_videoPanel->m_playerController)
            m_videoPanel->m_playerController->ResetVideoFilters();
        },
        idResetFilters);
  }
  videoMenu->AppendSubMenu(mirrorMenu, _U("Mirror / Filters"));

  // ---- Equalizer ----
  wxMenu *eqMenu = new wxMenu;
  {
    struct EqGroup {
      wxString label;
      const char *prop;
    };
    std::vector<EqGroup> groups = {{_U("Brightness"), "brightness"},
                                   {_U("Contrast"), "contrast"},
                                   {_U("Saturation"), "saturation"},
                                   {_U("Gamma"), "gamma"},
                                   {_U("Hue"), "hue"}};

    for (size_t g = 0; g < groups.size(); ++g) {
      const auto &grp = groups[g];

      std::string val;
      if (pc)
        pc->GetPropertyString(grp.prop, val);
      int idHeader = NewMenuId();
      eqMenu->Append(idHeader, wxString::Format(_U("%s: %s"), grp.label,
                                                wxString::FromUTF8(val)));
      eqMenu->Enable(idHeader, false);

      for (int delta : {+5, -5}) {
        int id = NewMenuId();
        wxString sign = (delta > 0) ? _U("+5") : _U("\u22125");
        eqMenu->Append(id, grp.label + " " + sign);
        std::string propCopy(grp.prop);
        eqMenu->Bind(
            wxEVT_MENU,
            [this, propCopy, delta](wxCommandEvent &) {
              if (m_videoPanel && m_videoPanel->m_playerController)
                m_videoPanel->m_playerController->SendCommand(
                    "add " + propCopy + " " + std::to_string(delta));
            },
            id);
      }

      if (g + 1 < groups.size())
        eqMenu->AppendSeparator();
    }

    eqMenu->AppendSeparator();
    int idEqReset = NewMenuId();
    eqMenu->Append(idEqReset, _U("Reset equalizer"));
    eqMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (!m_videoPanel)
            return;
          for (const char *p :
               {"brightness", "contrast", "saturation", "gamma", "hue"}) {
            m_videoPanel->ResetMpvPropertyToDefault(p);
          }
        },
        idEqReset);
  }
  videoMenu->AppendSubMenu(eqMenu, _U("Equalizer"));

  videoMenu->AppendSeparator();

  addRadioGroup(videoMenu, _U("Hardware Decoding"), "hwdec",
                {{_U("Auto-safe"), "auto-safe"},
                 {_U("Auto"), "auto"},
                 {_U("VAAPI"), "vaapi"},
                 {_U("VDPAU"), "vdpau"},
                 {_U("NVDEC"), "nvdec"},
                 {_U("Disabled"), "no"}});

  addRadioGroup(videoMenu, _U("Video Sync"), "video-sync",
                {{_U("Audio"), "audio"},
                 {_U("Display Resample"), "display-resample"},
                 {_U("Display Desync"), "display-desync"},
                 {_U("Display Tempo"), "display-tempo"}});

  addRadioGroup(videoMenu, _U("Scaling"), "scale",
                {{_U("Bilinear"), "bilinear"},
                 {_U("Bicubic"), "bicubic"},
                 {_U("Spline36"), "spline36"},
                 {_U("Lanczos"), "lanczos"},
                 {_U("EWA Lanczos"), "ewa_lanczos"}});

  addRadioGroup(videoMenu, _U("Drop Frames"), "framedrop",
                {{_U("Auto"), "auto"},
                 {_U("VO"), "vo"},
                 {_U("No"), "no"},
                 {_U("Decoder + VO"), "decoder+vo"}});

  // ---- Network Cache ----
  {
    wxMenu *cacheMenu = new wxMenu;
    int currentMB = 0;
    if (auto *cfg = getConfigManager())
      currentMB = cfg->getInt("mpv_cache_mb", 0);

    struct CacheItem {
      int mb;
      wxString label;
    };
    std::vector<CacheItem> items = {
        {0, _U("Default (mpv)")}, {32, _U("32 MB")},   {64, _U("64 MB")},
        {128, _U("128 MB")},      {256, _U("256 MB")}, {512, _U("512 MB")}};

    for (auto &it : items) {
      int id = NewMenuId();
      cacheMenu->AppendRadioItem(id, it.label);
      if (currentMB == it.mb)
        cacheMenu->Check(id, true);
      cacheMenu->Bind(
          wxEVT_MENU,
          [this, mb = it.mb](wxCommandEvent &) {
            if (!m_videoPanel)
              return;
            m_videoPanel->SetNetworkCacheMB(mb);
            SetStatusText(_U("Network cache will apply on next stream load."),
                          0);
          },
          id);
    }

    cacheMenu->AppendSeparator();
    int idCustom = NewMenuId();
    cacheMenu->Append(idCustom, _U("Custom\u2026"));
    cacheMenu->Bind(
        wxEVT_MENU,
        [this, currentMB](wxCommandEvent &) {
          wxNumberEntryDialog dlg(
              this, _U("Network cache size in MB:"),
              _U("0 = mpv default, 1-65536 = explicit size"),
              _U("Custom Network Cache"), currentMB, 0, 65536);
          if (dlg.ShowModal() != wxID_OK)
            return;
          int mb = dlg.GetValue();
          if (!m_videoPanel)
            return;
          m_videoPanel->SetNetworkCacheMB(mb);
          SetStatusText(
              wxString::Format(
                  _U("Network cache set to %d MB (applies on next stream)."),
                  mb),
              0);
        },
        idCustom);

    cacheMenu->AppendSeparator();
    int idResetCache = NewMenuId();
    cacheMenu->Append(idResetCache, _U("Reset to mpv default"));
    cacheMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (!m_videoPanel)
            return;
          m_videoPanel->ResetNetworkCacheToDefault();
          SetStatusText(_U("Network cache reset (applies on next stream)."), 0);
        },
        idResetCache);

    videoMenu->AppendSubMenu(cacheMenu, _U("Network Cache"));
  }

  videoMenu->AppendSeparator();

  addFlagToggle(videoMenu, _U("Interpolation"), "interpolation", "yes", "no",
                false);
  addFlagToggle(videoMenu, _U("Debanding"), "deband", "yes", "no", false);

  videoMenu->AppendSeparator();
  {
    int idResetAll = NewMenuId();
    videoMenu->Append(idResetAll, _U("Reset video options to mpv defaults"));
    videoMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (!m_videoPanel)
            return;
          for (const char *prop : {"hwdec", "framedrop", "video-sync", "scale",
                                   "interpolation", "deband"}) {
            m_videoPanel->ResetMpvPropertyToDefault(prop);
          }
        },
        idResetAll);
  }

  menu.AppendSubMenu(videoMenu, _U("Video"));

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
      trackMenu->Append(NewMenuId(), _U("(no audio tracks)"))->Enable(false);
    } else {
      for (const auto &[id, label] : tracks) {
        int menuId = NewMenuId();
        trackMenu->AppendRadioItem(menuId, label);
        if (id == currentAudio)
          trackMenu->Check(menuId, true);
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
  audioMenu->AppendSubMenu(trackMenu, _U("Track"));

  // ---- Delay ----
  wxMenu *delayMenu = new wxMenu;

  double currentDelay = 0.0;
  if (m_videoPanel && m_videoPanel->m_playerController)
    currentDelay = m_videoPanel->m_playerController->GetAudioDelay();

  int idCurrent = NewMenuId();
  delayMenu->Append(idCurrent,
                    wxString::Format(_U("Current: %+.2f s"), currentDelay));
  delayMenu->Enable(idCurrent, false);
  delayMenu->AppendSeparator();

  std::vector<double> delayValues = {-1.0, -0.5, -0.3, -0.2, -0.1, 0.0,
                                     0.1,  0.2,  0.3,  0.5,  1.0};
  for (double val : delayValues) {
    int id = NewMenuId();
    wxString label;
    if (val == 0.0)
      label = _U("Sync (0s)");
    else if (val > 0)
      label = wxString::Format(_U("+%.1fs"), val);
    else
      label = wxString::Format(_U("%.1fs"), val);
    delayMenu->AppendRadioItem(id, label);
    if (std::abs(currentDelay - val) < 0.01)
      delayMenu->Check(id, true);
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
  int idCustom = NewMenuId();
  delayMenu->Append(idCustom, _U("Custom..."));
  delayMenu->Bind(
      wxEVT_MENU,
      [this](wxCommandEvent &) {
        if (m_videoPanel && m_videoPanel->m_playerController) {
          wxTextEntryDialog dlg(
              this, _U("Enter audio delay in seconds (e.g., -0.5 or 0.3):"),
              _U("Custom Audio Delay"), "0.0");
          if (dlg.ShowModal() == wxID_OK) {
            wxString input = dlg.GetValue();
            double val;
            if (input.ToDouble(&val)) {
              m_videoPanel->m_playerController->SetAudioDelay(val);
            } else {
              wxMessageBox(_U("Invalid number. Please enter a numeric value."),
                           _U("Error"), wxOK | wxICON_ERROR, this);
            }
          }
        }
      },
      idCustom);

  audioMenu->AppendSubMenu(delayMenu, _U("Delay"));

  // ---- Speed ----
  wxMenu *speedMenu = new wxMenu;
  {
    double currentSpeed = 1.0;
    std::string speedStr;
    if (pc && pc->GetPropertyString("speed", speedStr) && !speedStr.empty()) {
      try {
        currentSpeed = std::stod(speedStr);
      } catch (...) {
        currentSpeed = 1.0;
      }
    }

    int idCurSpeed = NewMenuId();
    speedMenu->Append(
        idCurSpeed, wxString::Format(_U("Current: %.2f\u00d7"), currentSpeed));
    speedMenu->Enable(idCurSpeed, false);
    speedMenu->AppendSeparator();

    std::vector<std::pair<wxString, double>> speedItems = {
        {_U("0.5\u00d7"), 0.5}, {_U("0.75\u00d7"), 0.75},
        {_U("1.0\u00d7"), 1.0}, {_U("1.25\u00d7"), 1.25},
        {_U("1.5\u00d7"), 1.5}, {_U("2.0\u00d7"), 2.0}};
    for (auto &it : speedItems) {
      int id = NewMenuId();
      speedMenu->AppendRadioItem(id, it.first);
      double val = it.second;
      if (std::abs(currentSpeed - val) < 0.001)
        speedMenu->Check(id, true);
      speedMenu->Bind(
          wxEVT_MENU,
          [this, val](wxCommandEvent &) {
            if (m_videoPanel)
              m_videoPanel->SetMpvPropertyAndPersist("speed",
                                                     std::to_string(val));
          },
          id);
    }
    speedMenu->AppendSeparator();
    int idResetSpeed = NewMenuId();
    speedMenu->Append(idResetSpeed, _U("Reset to 1.0\u00d7"));
    speedMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel)
            m_videoPanel->ResetMpvPropertyToDefault("speed");
        },
        idResetSpeed);
  }
  audioMenu->AppendSubMenu(speedMenu, _U("Speed"));

  // ---- Channels ----
  wxMenu *chMenu = new wxMenu;
  {
    std::vector<const char *> channels = {"auto", "mono", "stereo", "5.1",
                                          "7.1"};
    std::string currentCh;
    if (pc)
      pc->GetPropertyString("audio-channels", currentCh);

    for (const char *ch : channels) {
      int id = NewMenuId();
      chMenu->AppendRadioItem(id, _U(ch));
      if (currentCh == ch)
        chMenu->Check(id, true);
      std::string chVal(ch);
      chMenu->Bind(
          wxEVT_MENU,
          [this, chVal](wxCommandEvent &) {
            if (m_videoPanel)
              m_videoPanel->SetMpvPropertyAndPersist("audio-channels", chVal);
          },
          id);
    }
    chMenu->AppendSeparator();
    int idResetCh = NewMenuId();
    chMenu->Append(idResetCh, _U("Reset to default"));
    chMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel)
            m_videoPanel->ResetMpvPropertyToDefault("audio-channels");
        },
        idResetCh);
  }
  audioMenu->AppendSubMenu(chMenu, _U("Channels"));

  // ---- Device ----
  wxMenu *deviceMenu = new wxMenu;
  {
    std::string currentDevice;
    if (pc)
      pc->GetPropertyString("audio-device", currentDevice);

    auto devices =
        pc ? pc->GetAudioDevices() : std::vector<IPlayerBackend::AudioDevice>{};

    if (devices.empty()) {
      deviceMenu->Append(NewMenuId(), _U("(no audio devices)"))->Enable(false);
    } else {
      for (auto &dev : devices) {
        int id = NewMenuId();
        deviceMenu->AppendRadioItem(id, wxString::FromUTF8(dev.second));
        if (dev.first == currentDevice)
          deviceMenu->Check(id, true);
        std::string devName = dev.first;
        deviceMenu->Bind(
            wxEVT_MENU,
            [this, devName](wxCommandEvent &) {
              if (m_videoPanel)
                m_videoPanel->SetMpvPropertyAndPersist("audio-device", devName);
            },
            id);
      }
    }

    deviceMenu->AppendSeparator();
    int idResetDev = NewMenuId();
    deviceMenu->Append(idResetDev, _U("Reset to default"));
    deviceMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel)
            m_videoPanel->ResetMpvPropertyToDefault("audio-device");
        },
        idResetDev);
  }
  audioMenu->AppendSubMenu(deviceMenu, _U("Device"));

  // ---- Passthrough (S/PDIF, HDMI) ----
  wxMenu *passthroughMenu = new wxMenu;
  {
    auto *cfg = getConfigManager();
    std::string current = cfg ? cfg->getSetting("mpv_audio_spdif", "") : "";

    struct PtItem {
      wxString label;
      const char *value;
    };
    std::vector<PtItem> items = {
        {_U("Off"), ""},
        {_U("AC3 & DTS"), "ac3,dts"},
        {_U("AC3, DTS, E-AC3, TrueHD, DTS-HD"), "ac3,dts,eac3,truehd,dts-hd"}};

    for (const auto &it : items) {
      int id = NewMenuId();
      passthroughMenu->AppendRadioItem(id, it.label);
      if (current == it.value)
        passthroughMenu->Check(id, true);
      std::string val(it.value);
      passthroughMenu->Bind(
          wxEVT_MENU,
          [this, val](wxCommandEvent &) {
            if (!m_videoPanel)
              return;
            m_videoPanel->SetMpvPropertyAndPersist("audio-spdif", val);
            SetStatusText(_U("Passthrough will apply on next stream load."), 0);
          },
          id);
    }
  }
  audioMenu->AppendSubMenu(passthroughMenu, _U("Passthrough (S/PDIF, HDMI)"));

  audioMenu->AppendSeparator();
  addFlagToggle(audioMenu, _U("Pitch Correction"), "audio-pitch-correction",
                "yes", "no", true);

  audioMenu->AppendSeparator();
  {
    int idResetAudio = NewMenuId();
    audioMenu->Append(idResetAudio, _U("Reset audio options to mpv defaults"));
    audioMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (!m_videoPanel)
            return;
          for (const char *p : {"audio-pitch-correction", "speed",
                                "audio-channels", "audio-device"}) {
            m_videoPanel->ResetMpvPropertyToDefault(p);
          }
        },
        idResetAudio);
  }

  menu.AppendSubMenu(audioMenu, _U("Audio"));

  // ---- Submenu "Record" ----
  wxMenu *recordMenu = new wxMenu;

  int idRecSetDir = NewMenuId();
  recordMenu->Append(idRecSetDir, _U("Set Record Directory\u2026"));
  recordMenu->Bind(
      wxEVT_MENU,
      [this](wxCommandEvent &) {
        if (!m_videoPanel)
          return;
        wxDirDialog dlg(this, _U("Choose record directory"),
                        m_videoPanel->GetRecordDirectory(), wxDD_DEFAULT_STYLE);
        if (dlg.ShowModal() == wxID_OK) {
          m_videoPanel->SetRecordDirectory(dlg.GetPath());
          auto *cfg = getConfigManager();
          if (cfg)
            cfg->setSetting("record_directory", dlg.GetPath().ToUTF8().data());
        }
      },
      idRecSetDir);

  int idRecOpenFolder = NewMenuId();
  recordMenu->Append(idRecOpenFolder, _U("Open Recordings Folder"));
  recordMenu->Bind(
      wxEVT_MENU,
      [this](wxCommandEvent &) {
        if (m_videoPanel) {
          wxString dir = m_videoPanel->GetRecordDirectory();
          if (wxDirExists(dir))
            wxLaunchDefaultApplication(dir);
        }
      },
      idRecOpenFolder);
  menu.AppendSubMenu(recordMenu, _U("Record"));

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
      subMenu->Append(NewMenuId(), _U("(no subtitle tracks)"))->Enable(false);
    } else {
      for (const auto &[id, label] : tracks) {
        int menuId = NewMenuId();
        subMenu->AppendRadioItem(menuId, label);
        if (id == currentSub)
          subMenu->Check(menuId, true);
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
  subMenu->AppendSeparator();

  // ---- Delay ----
  wxMenu *subDelayMenu = new wxMenu;
  {
    double currentSubDelay = 0.0;
    if (pc)
      currentSubDelay = pc->GetSubtitleDelay();

    int idCurSubDelay = NewMenuId();
    subDelayMenu->Append(idCurSubDelay, wxString::Format(_U("Current: %+.2f s"),
                                                         currentSubDelay));
    subDelayMenu->Enable(idCurSubDelay, false);
    subDelayMenu->AppendSeparator();

    std::vector<std::pair<wxString, double>> items = {{_U("\u22120.5s"), -0.5},
                                                      {_U("\u22120.1s"), -0.1},
                                                      {_U("Reset (0.0s)"), 0.0},
                                                      {_U("+0.1s"), 0.1},
                                                      {_U("+0.5s"), 0.5}};
    for (auto &it : items) {
      int id = NewMenuId();
      subDelayMenu->AppendRadioItem(id, it.first);
      double val = it.second;
      if (std::abs(currentSubDelay - val) < 0.01)
        subDelayMenu->Check(id, true);
      subDelayMenu->Bind(
          wxEVT_MENU,
          [this, val](wxCommandEvent &) {
            if (m_videoPanel && m_videoPanel->m_playerController)
              m_videoPanel->m_playerController->SetPropertyString(
                  "sub-delay", std::to_string(val));
          },
          id);
    }
  }
  subMenu->AppendSubMenu(subDelayMenu, _U("Delay"));

  // ---- Scale ----
  wxMenu *subScaleMenu = new wxMenu;
  {
    double currentSubScale = 1.0;
    if (pc)
      currentSubScale = pc->GetSubtitleScale();

    int idCurSubScale = NewMenuId();
    subScaleMenu->Append(
        idCurSubScale,
        wxString::Format(_U("Current: %.2f\u00d7"), currentSubScale));
    subScaleMenu->Enable(idCurSubScale, false);
    subScaleMenu->AppendSeparator();

    std::vector<double> scales = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    for (double s : scales) {
      int id = NewMenuId();
      subScaleMenu->AppendRadioItem(id, wxString::Format(_U("%.2f\u00d7"), s));
      if (std::abs(currentSubScale - s) < 0.01)
        subScaleMenu->Check(id, true);
      subScaleMenu->Bind(
          wxEVT_MENU,
          [this, s](wxCommandEvent &) {
            if (m_videoPanel)
              m_videoPanel->SetMpvPropertyAndPersist("sub-scale",
                                                     std::to_string(s));
          },
          id);
    }
    subScaleMenu->AppendSeparator();
    int idResetScale = NewMenuId();
    subScaleMenu->Append(idResetScale, _U("Reset to default"));
    subScaleMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel)
            m_videoPanel->ResetMpvPropertyToDefault("sub-scale");
        },
        idResetScale);
  }
  subMenu->AppendSubMenu(subScaleMenu, _U("Scale"));

  // ---- Position ----
  wxMenu *subPosMenu = new wxMenu;
  {
    int currentSubPos = 100;
    if (pc)
      currentSubPos = pc->GetSubtitlePos();

    int idCurSubPos = NewMenuId();
    subPosMenu->Append(idCurSubPos,
                       wxString::Format(_U("Current: %d"), currentSubPos));
    subPosMenu->Enable(idCurSubPos, false);
    subPosMenu->AppendSeparator();

    struct PosItem {
      int value;
      wxString label;
    };
    std::vector<PosItem> positions = {{100, _U("100 (Default)")},
                                      {90, _U("90 (Above UI)")},
                                      {80, _U("80 (Letterbox)")},
                                      {75, _U("75 (Upper letterbox)")},
                                      {50, _U("50 (Center)")}};
    for (auto &it : positions) {
      int id = NewMenuId();
      subPosMenu->AppendRadioItem(id, it.label);
      if (currentSubPos == it.value)
        subPosMenu->Check(id, true);
      int val = it.value;
      subPosMenu->Bind(
          wxEVT_MENU,
          [this, val](wxCommandEvent &) {
            if (m_videoPanel)
              m_videoPanel->SetMpvPropertyAndPersist("sub-pos",
                                                     std::to_string(val));
          },
          id);
    }
    subPosMenu->AppendSeparator();
    int idResetPos = NewMenuId();
    subPosMenu->Append(idResetPos, _U("Reset to default"));
    subPosMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (m_videoPanel)
            m_videoPanel->ResetMpvPropertyToDefault("sub-pos");
        },
        idResetPos);
  }
  subMenu->AppendSubMenu(subPosMenu, _U("Position"));

  subMenu->AppendSeparator();
  {
    int idSubReset = NewMenuId();
    subMenu->Append(idSubReset, _U("Reset subtitle options"));
    subMenu->Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          if (!m_videoPanel)
            return;
          for (const char *p : {"sub-delay", "sub-scale", "sub-pos"}) {
            m_videoPanel->ResetMpvPropertyToDefault(p);
          }
        },
        idSubReset);
  }

  menu.AppendSubMenu(subMenu, _U("Subtitles"));

  menu.AppendSeparator();
  menu.Append(ID_MENU_SETTINGS, _U("Settings"));
  menu.Append(ID_MENU_ABOUT, _U("About"));
  menu.Append(ID_MENU_EXIT, _U("Quit"));

  fprintf(stderr, "[MARK] M2: menu built\n");
  fflush(stderr);

  auto doShow = [this, menuPtr]() {
    fprintf(stderr, "[MARK] M3: showing PopupMenu\n");
    fflush(stderr);
    PopupMenu(menuPtr.get());
    fprintf(stderr, "[MARK] M4: PopupMenu returned\n");
    fflush(stderr);
  };

  if (deferred) {
    CallAfter(doShow);
  } else {
    doShow();
  }
}
