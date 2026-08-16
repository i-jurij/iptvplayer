#include "SettingsDialog.h"
#include "ConfigManager.h"
#include "ErrorCode.h"
#include "IconManager.h"
#include "LogControl.h"
#include "LogoCache.h"
#include "MainFrame.h"
#include "Utils.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/statline.h>
#include <wx/textdlg.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <wx/wx.h>

#include <algorithm>
#include <cctype>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

SettingsDialog::SettingsDialog(MainFrame *parent, ConfigManager *cfg)
    : wxDialog(parent, wxID_ANY, "Settings", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_config(cfg), m_mainFrame(parent) {
  const wxSize basePadDLU(6, 6);
  const wxSize innerPadDLU(5, 5);
  const int PAD = this->ConvertDialogToPixels(basePadDLU).x;
  const int INNER_PAD = this->ConvertDialogToPixels(innerPadDLU).x;

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  wxScrolledWindow *scrolled = new wxScrolledWindow(
      this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
  scrolled->SetScrollRate(5, 5);

  wxBoxSizer *contentSizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText *thumbHint = nullptr;
  wxStaticText *cacheNote = nullptr;
  wxStaticText *logoNote = nullptr;
  wxStaticText *plNote = nullptr;

  // -------------------------
  // Block 1: User interface — Logo Options
  // -------------------------
  {
    wxStaticBox *logoBox =
        new wxStaticBox(scrolled, wxID_ANY, "User interface — Logo Options");
    wxStaticBoxSizer *logoBoxSizer = new wxStaticBoxSizer(logoBox, wxVERTICAL);

    thumbHint =
        new wxStaticText(logoBox, wxID_ANY,
                         "Note: this feature is experimental and may use "
                         "significant memory for very large playlists.\nLogo "
                         "in list view wiil be show after restart.");
    wxFont hintFont = thumbHint->GetFont();
    hintFont.SetPointSize(hintFont.GetPointSize() - 1);
    thumbHint->SetFont(hintFont);
    thumbHint->SetForegroundColour(*wxLIGHT_GREY);
    logoBoxSizer->Add(thumbHint, 0, wxALL | wxEXPAND, INNER_PAD);

    bool nologo = (m_config->getSetting("nologo", "true") == "true");
    m_noLogoCheck =
        new wxCheckBox(logoBox, wxID_ANY, "Show channel logos (thumbnails)");
    m_noLogoCheck->SetValue(!nologo);
    logoBoxSizer->Add(m_noLogoCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM,
                      INNER_PAD / 2);

    m_deleteLogoCacheOnDisableCheck = new wxCheckBox(
        logoBox, wxID_ANY, "Delete logo cache (disk and memory)");
    m_deleteLogoCacheOnDisableCheck->SetToolTip(
        "If checked, when you disable channel logos the on-disk and in-memory "
        "logo cache will be deleted when you apply settings.");
    logoBoxSizer->Add(m_deleteLogoCacheOnDisableCheck, 0,
                      wxLEFT | wxRIGHT | wxBOTTOM, INNER_PAD / 2);

    contentSizer->Add(logoBoxSizer, 0, wxEXPAND | wxALL, PAD);
  }

  // -------------------------
  // Block 2: Logo maintenance
  // -------------------------
  {
    wxStaticBox *actionsBox =
        new wxStaticBox(scrolled, wxID_ANY, "Logo maintenance");
    wxStaticBoxSizer *actionsSizer =
        new wxStaticBoxSizer(actionsBox, wxVERTICAL);

    cacheNote = new wxStaticText(actionsBox, wxID_ANY,
                                 "Disk cache removes stored icon files; RAM "
                                 "cache clears loaded images.");
    cacheNote->SetForegroundColour(*wxLIGHT_GREY);
    actionsSizer->Add(cacheNote, 0, wxALL | wxEXPAND, INNER_PAD);

    m_deleteDiskCacheBtn =
        new wxButton(actionsBox, wxID_ANY, "Delete disk cache");
    actionsSizer->Add(m_deleteDiskCacheBtn, 0,
                      wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_LEFT, INNER_PAD);

    m_deleteRamCacheBtn =
        new wxButton(actionsBox, wxID_ANY, "Delete RAM cache");
    actionsSizer->Add(m_deleteRamCacheBtn, 0,
                      wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_LEFT, INNER_PAD);

    logoNote = new wxStaticText(
        actionsBox, wxID_ANY,
        "Use this to force re-download of logos that previously failed.");
    logoNote->SetForegroundColour(*wxLIGHT_GREY);
    actionsSizer->Add(logoNote, 0, wxALL | wxEXPAND, INNER_PAD);

    m_reloadMissingLogosBtn =
        new wxButton(actionsBox, wxID_ANY, "Reload missing logos");
    actionsSizer->Add(m_reloadMissingLogosBtn, 0,
                      wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_LEFT, INNER_PAD);

    contentSizer->Add(actionsSizer, 0, wxEXPAND | wxALL, PAD);
  }

  // -------------------------
  // Block 3: Playlists
  // -------------------------
  {
    wxStaticBox *plBox = new wxStaticBox(scrolled, wxID_ANY, "Playlists");
    wxStaticBoxSizer *plBoxSizer = new wxStaticBoxSizer(plBox, wxVERTICAL);

    plNote = new wxStaticText(plBox, wxID_ANY,
                              "Playlist auto-update and related settings.");
    plNote->SetForegroundColour(*wxLIGHT_GREY);
    plBoxSizer->Add(plNote, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, INNER_PAD);

    bool autoUpdate = (m_config->getSetting("auto_update", "false") == "true");
    m_autoUpdateCheck =
        new wxCheckBox(plBox, wxID_ANY, "Enable playlists auto update");
    m_autoUpdateCheck->SetValue(autoUpdate);
    plBoxSizer->Add(m_autoUpdateCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM,
                    INNER_PAD / 2);

    contentSizer->Add(plBoxSizer, 0, wxEXPAND | wxALL, PAD);
  }

  contentSizer->AddStretchSpacer(1);

  scrolled->SetSizer(contentSizer);
  scrolled->Layout();
  scrolled->FitInside();

  // Bottom buttons
  wxBoxSizer *bottomSizer = new wxBoxSizer(wxHORIZONTAL);

  wxButton *restoreBtn = new wxButton(this, wxID_ANY, "Restore Defaults");
  bottomSizer->Add(restoreBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, PAD);

  bottomSizer->AddStretchSpacer(1);

  wxButton *applyBtn = new wxButton(this, wxID_APPLY, "Apply");
  wxButton *okBtn = new wxButton(this, wxID_OK, "OK");
  wxButton *cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");

  applyBtn->SetMinSize(applyBtn->GetBestSize());
  okBtn->SetMinSize(okBtn->GetBestSize());
  cancelBtn->SetMinSize(cancelBtn->GetBestSize());

  int gap = std::max(6, PAD / 2);
  bottomSizer->Add(applyBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
  bottomSizer->Add(okBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
  bottomSizer->Add(cancelBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, PAD);

  mainSizer->Add(scrolled, 1, wxEXPAND | wxALL, PAD);
  mainSizer->Add(bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, PAD);

  SetSizer(mainSizer);
  mainSizer->SetSizeHints(this);

  // Sizing logic similar to original
  wxSize contentEstimate = contentSizer->CalcMin();
  int rightButtonsTotalW = applyBtn->GetBestSize().GetWidth() +
                           okBtn->GetBestSize().GetWidth() +
                           cancelBtn->GetBestSize().GetWidth();
  int computedBottomWidth = restoreBtn->GetBestSize().GetWidth() +
                            rightButtonsTotalW + 3 * PAD + 2 * gap;
  int desiredWidth = std::max(contentEstimate.GetWidth(), computedBottomWidth);

  wxSize displaySize = wxGetDisplaySize();
  const int SCREEN_MARGIN = this->ConvertDialogToPixels(wxSize(10, 10)).y;
  int maxAllowedWidth = displaySize.GetWidth() - 2 * SCREEN_MARGIN;
  int maxAllowedHeight = displaySize.GetHeight() - 2 * SCREEN_MARGIN;
  desiredWidth = std::min(desiredWidth, maxAllowedWidth);

  int availW = desiredWidth - 2 * PAD - 20;
  if (availW < 80)
    availW = 80;

  if (thumbHint)
    thumbHint->Wrap(availW);
  if (cacheNote)
    cacheNote->Wrap(availW);
  if (logoNote)
    logoNote->Wrap(availW);
  if (plNote)
    plNote->Wrap(availW);

  contentSizer->Layout();
  scrolled->FitInside();
  wxSize contentMin = contentSizer->CalcMin();

  desiredWidth = std::max(desiredWidth, contentMin.GetWidth());
  desiredWidth = std::min(desiredWidth, maxAllowedWidth);

  wxSize bottomMin = bottomSizer->CalcMin();
  int desiredHeight = contentMin.GetHeight() + bottomMin.GetHeight() + 3 * PAD;
  if (desiredHeight > maxAllowedHeight) {
    desiredHeight = maxAllowedHeight;
    int scrolledH = maxAllowedHeight - bottomMin.GetHeight() - 3 * PAD;
    if (scrolledH < 80)
      scrolledH = 80;
    scrolled->SetMinSize(wxSize(-1, scrolledH));
  } else {
    scrolled->SetMinSize(wxSize(-1, contentMin.GetHeight()));
  }

  int minWidth = std::min(maxAllowedWidth, computedBottomWidth);
  int minHeight =
      std::min(maxAllowedHeight, this->ConvertDialogToPixels(wxSize(0, 200)).y);
  this->SetMinSize(wxSize(minWidth, minHeight));

  this->SetSize(wxSize(desiredWidth, desiredHeight));
  Layout();
  scrolled->FitInside();
  scrolled->SetScrollRate(5, 5);

  CentreOnParent();

  // Bind handlers
  Bind(wxEVT_BUTTON, &SettingsDialog::OnApply, this, wxID_APPLY);
  Bind(wxEVT_BUTTON, &SettingsDialog::OnOk, this, wxID_OK);
  Bind(wxEVT_BUTTON, &SettingsDialog::OnCancel, this, wxID_CANCEL);
  restoreBtn->Bind(wxEVT_BUTTON, &SettingsDialog::OnRestoreDefaults, this);

  m_deleteDiskCacheBtn->Bind(wxEVT_BUTTON,
                             &SettingsDialog::OnDeleteDiskCacheNow, this);
  m_deleteRamCacheBtn->Bind(wxEVT_BUTTON, &SettingsDialog::OnDeleteRamCacheNow,
                            this);
  m_reloadMissingLogosBtn->Bind(wxEVT_BUTTON,
                                &SettingsDialog::OnReloadMissingLogos, this);
}

void SettingsDialog::OnApply(wxCommandEvent &WXUNUSED(event)) {
  ApplySettings(true);
  wxLogStatus("Settings applied");
  m_mainFrame->SetStatusText("Settings applied.", 0);
}

void SettingsDialog::OnOk(wxCommandEvent &WXUNUSED(event)) {
  ApplySettings(true);
  EndModal(wxID_OK);
}

void SettingsDialog::OnCancel(wxCommandEvent &WXUNUSED(event)) {
  EndModal(wxID_CANCEL);
}

void SettingsDialog::RestoreDefaults() {
  const bool default_show_logos = false; // nologo = true
  const bool default_auto_update = false;

  if (m_noLogoCheck)
    m_noLogoCheck->SetValue(default_show_logos);

  if (m_autoUpdateCheck)
    m_autoUpdateCheck->SetValue(default_auto_update);

  // Для EPG сброс не реализован — оставляем текущие настройки,
  // пользователь может изменить их вручную.
}

void SettingsDialog::OnRestoreDefaults(wxCommandEvent &WXUNUSED(event)) {
  RestoreDefaults();
}

void SettingsDialog::ApplySettings(bool saveConfig) {
  if (!m_mainFrame)
    return;

  bool show = m_noLogoCheck ? m_noLogoCheck->GetValue() : false;

  m_mainFrame->SetShowLogoFromSettings(show);

  if (!show && m_deleteLogoCacheOnDisableCheck &&
      m_deleteLogoCacheOnDisableCheck->GetValue()) {
    MainFrame *mf = m_mainFrame;

    std::thread([mf, this]() {
      ErrorCode ec = this->ClearAllCachesSync();
      wxTheApp->CallAfter([mf, ec]() {
        if (!mf)
          return;
        if (ec == ErrorCode::OK) {
          mf->SetStatusText("Logo cache deleted (disk and memory).", 1);
        } else if (ec == ErrorCode::FileNotFound) {
          mf->SetStatusText("Logo cache: nothing to delete.", 1);
        } else if (ec == ErrorCode::UnsafePath) {
          mf->SetStatusText("Refused to delete icons: unsafe path.", 1);
        } else {
          mf->SetStatusText("Failed to delete logo cache. See log.", 1);
        }
      });
    }).detach();
  }

  if (saveConfig && m_config) {
    m_config->setSetting("nologo", show ? "false" : "true");
    m_config->setSetting(
        "auto_update",
        m_autoUpdateCheck && m_autoUpdateCheck->GetValue() ? "true" : "false");
    m_config->saveSettings();
    m_mainFrame->SetStatusText("Settings saved.", 0);
  }
}

void SettingsDialog::OnDeleteDiskCacheNow(wxCommandEvent &WXUNUSED(event)) {
  wxMessageDialog dlg(this, "Delete on-disk logo cache (icon files) from disk?",
                      "Confirm delete disk cache", wxYES_NO | wxICON_WARNING);
  if (dlg.ShowModal() != wxID_YES) {
    LOG_DEBUG("OnDeleteDiskCacheNow: user cancelled disk cache deletion");
    return;
  }

  if (m_mainFrame) {
    m_mainFrame->SetStatusText("Deleting on-disk logo cache...", 0);
  }

  MainFrame *mf = m_mainFrame;
  std::thread([mf]() {
    ErrorCode ec = IconManager::DeleteAllIcons();
    wxTheApp->CallAfter([mf, ec]() {
      if (!mf)
        return;
      if (ec == ErrorCode::OK) {
        mf->SetStatusText("Disk logo cache deleted.", 1);
      } else if (ec == ErrorCode::FileNotFound) {
        mf->SetStatusText("Disk logo cache: nothing to delete.", 1);
      } else {
        mf->SetStatusText("Failed to delete disk logo cache. See log.", 1);
      }
    });
  }).detach();
}

void SettingsDialog::OnDeleteRamCacheNow(wxCommandEvent &WXUNUSED(event)) {
  wxMessageDialog dlg(this, "Clear in-memory logo cache (RAM)?",
                      "Confirm clear RAM cache", wxYES_NO | wxICON_QUESTION);
  if (dlg.ShowModal() != wxID_YES) {
    LOG_DEBUG("OnDeleteRamCacheNow: user cancelled RAM cache clear");
    return;
  }

  LogoCache::ClearAll();
  wxLogInfo("OnDeleteRamCacheNow: in-memory logo cache cleared.");

  if (m_mainFrame) {
    m_mainFrame->SetStatusText("In-memory logo cache cleared.", 0);
  }
}

void SettingsDialog::OnReloadMissingLogos(wxCommandEvent &) {
  wxString iconsBase =
      wxString::FromUTF8(IconManager::GetCacheDir()) + "/icons";

  if (!wxDirExists(iconsBase)) {
    if (m_mainFrame)
      m_mainFrame->SetStatusText("No icon cache found (nothing to reload).", 0);
    return;
  }

  if (m_mainFrame)
    m_mainFrame->SetStatusText("Removing .marker files...", 0);

  std::thread([iconsBase, this]() {
    size_t removed = 0, skipped = 0, failed = 0;
    RemoveMarkerFilesRecursive(iconsBase, removed, skipped, failed, false);
    size_t found = removed + skipped + failed;

    LogoCache::ClearAll();

    wxString msg = wxString::Format(
        "Found %zu marker(s). Removed %zu; skipped %zu unsafe; failed %zu.",
        found, removed, skipped, failed);

    wxTheApp->CallAfter([this, msg]() {
      if (m_mainFrame)
        m_mainFrame->SetStatusText(msg, 0);
      else
        wxLogInfo(msg);
    });
  }).detach();
}

ErrorCode SettingsDialog::ClearAllCachesSync() {
  ChannelCards *channelCards = nullptr;
  if (m_mainFrame) {
    channelCards = m_mainFrame->GetChannelCards();
  }

  if (!channelCards) {
    wxLogWarning("ClearAllCachesSync: ChannelCards not available; proceeding "
                 "with disk-only cleanup.");
  } else {
    channelCards->PauseLogoLoading();
    channelCards->ClearAllCaches(true, true);
  }

  ErrorCode ec = IconManager::DeleteAllIcons();

  if (ec == ErrorCode::OK) {
    LogoCache::ClearAll();
    wxLogInfo("ClearAllCachesSync: disk icons removed and LogoCache::ClearAll "
              "called");
  } else if (ec == ErrorCode::FileNotFound) {
    wxLogInfo("ClearAllCachesSync: no disk icon files found to remove");
  } else {
    wxLogWarning(
        "ClearAllCachesSync: IconManager::DeleteAllIcons returned code=%d",
        static_cast<int>(ec));
  }

  if (channelCards) {
    channelCards->InvalidateAll();
    channelCards->ResumeLogoLoading();
  }

  return ec;
}

