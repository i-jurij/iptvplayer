#include "SettingsDialog.h"
#include "ConfigManager.h"
#include "ErrorCode.h"
#include "IconManager.h"
#include "LogoCache.h"
#include "MainFrame.h"
#include "Utils.h"
#include "epg/AddEpgSourceDialog.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/statline.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <wx/wx.h>

#include <algorithm>
#include <cctype> // std::tolower
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
  // Dialog units -> pixels so spacing follows system font/DPI
  const wxSize basePadDLU(6, 6);
  const wxSize innerPadDLU(5, 5);
  const int PAD = this->ConvertDialogToPixels(basePadDLU).x;
  const int INNER_PAD = this->ConvertDialogToPixels(innerPadDLU).x;

  // Main vertical sizer
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // Scrolled area: allow both scrollbars, but we'll avoid horizontal scrollbar
  // by wrapping static texts to the computed available width before sizing.
  wxScrolledWindow *scrolled = new wxScrolledWindow(
      this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
  scrolled->SetScrollRate(5, 5);

  // Content sizer inside scrolled window
  wxBoxSizer *contentSizer = new wxBoxSizer(wxVERTICAL);

  // Pointers to wrapable static texts
  wxStaticText *thumbHint = nullptr;
  wxStaticText *cacheNote = nullptr;
  wxStaticText *logoNote = nullptr;
  wxStaticText *plNote = nullptr;

  // -------------------------
  // Block 1: User interface — Logo Options (static box)
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
  // Block 2: Logo maintenance (static box)
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
  // Block 4: Playlists (static box)
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

  // Flexible spacer
  contentSizer->AddStretchSpacer(1);

  // Assign sizer to scrolled (initial)
  scrolled->SetSizer(contentSizer);
  scrolled->Layout();
  scrolled->FitInside();

  // -------------------------
  // Block: EPG (Electronic Program Guide)
  // -------------------------
  {
    wxStaticBox *epgBox =
        new wxStaticBox(scrolled, wxID_ANY, "EPG (Electronic Program Guide)");
    wxStaticBoxSizer *epgBoxSizer = new wxStaticBoxSizer(epgBox, wxVERTICAL);

    // --- Sources list ---
    m_epgSourceList =
        new wxListCtrl(epgBox, wxID_ANY, wxDefaultPosition, wxSize(-1, 120),
                       wxLC_REPORT | wxLC_SINGLE_SEL);
    m_epgSourceList->InsertColumn(0, "URL", wxLIST_FORMAT_LEFT, 300);
    m_epgSourceList->InsertColumn(1, "Name", wxLIST_FORMAT_LEFT, 150);
    m_epgSourceList->InsertColumn(2, "Last Update", wxLIST_FORMAT_LEFT, 150);
    epgBoxSizer->Add(m_epgSourceList, 0, wxEXPAND | wxALL, INNER_PAD);

    // --- Source buttons ---
    wxBoxSizer *sourceBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_epgAddSource = new wxButton(epgBox, wxID_ANY, "Add");
    m_epgEditSource = new wxButton(epgBox, wxID_ANY, "Edit");
    m_epgRemoveSource = new wxButton(epgBox, wxID_ANY, "Remove");
    sourceBtnSizer->Add(m_epgAddSource, 0, wxRIGHT, 5);
    sourceBtnSizer->Add(m_epgEditSource, 0, wxRIGHT, 5);
    sourceBtnSizer->Add(m_epgRemoveSource, 0);
    epgBoxSizer->Add(sourceBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, INNER_PAD);

    // --- Update settings ---
    wxFlexGridSizer *grid = new wxFlexGridSizer(2, 5, 10);
    grid->AddGrowableCol(1);

    grid->Add(new wxStaticText(epgBox, wxID_ANY, "Auto-update:"), 0,
              wxALIGN_CENTER_VERTICAL);
    m_epgAutoUpdate = new wxCheckBox(epgBox, wxID_ANY, "");
    grid->Add(m_epgAutoUpdate, 0, wxEXPAND);

    grid->Add(new wxStaticText(epgBox, wxID_ANY, "Update interval (hours):"), 0,
              wxALIGN_CENTER_VERTICAL);
    m_epgUpdateInterval = new wxSpinCtrl(epgBox, wxID_ANY, wxEmptyString,
                                         wxDefaultPosition, wxSize(80, -1));
    m_epgUpdateInterval->SetRange(1, 168);
    m_epgUpdateInterval->SetValue(24);
    grid->Add(m_epgUpdateInterval, 0, wxEXPAND);

    grid->Add(new wxStaticText(epgBox, wxID_ANY, "Days to keep in cache:"), 0,
              wxALIGN_CENTER_VERTICAL);
    m_epgDaysToKeep = new wxSpinCtrl(epgBox, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxSize(80, -1));
    m_epgDaysToKeep->SetRange(1, 30);
    m_epgDaysToKeep->SetValue(3);
    grid->Add(m_epgDaysToKeep, 0, wxEXPAND);

    epgBoxSizer->Add(grid, 0, wxEXPAND | wxALL, INNER_PAD);

    // --- Action buttons ---
    wxBoxSizer *actionBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_epgRefreshNow = new wxButton(epgBox, wxID_ANY, "Refresh Now");
    m_epgDeleteCache = new wxButton(epgBox, wxID_ANY, "Delete Cache");
    actionBtnSizer->Add(m_epgRefreshNow, 0, wxRIGHT, 5);
    actionBtnSizer->Add(m_epgDeleteCache, 0);
    epgBoxSizer->Add(actionBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, INNER_PAD);

    contentSizer->Add(epgBoxSizer, 0, wxEXPAND | wxALL, PAD);
  }

  // -------------------------
  // Bottom buttons (single block)
  // -------------------------
  wxBoxSizer *bottomSizer = new wxBoxSizer(wxHORIZONTAL);

  wxButton *restoreBtn = new wxButton(this, wxID_ANY, "Restore Defaults");
  bottomSizer->Add(restoreBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, PAD);

  bottomSizer->AddStretchSpacer(1);

  wxButton *applyBtn = new wxButton(this, wxID_APPLY, "Apply");
  wxButton *okBtn = new wxButton(this, wxID_OK, "OK");
  wxButton *cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");

  // Protect buttons from being shrunk
  applyBtn->SetMinSize(applyBtn->GetBestSize());
  okBtn->SetMinSize(okBtn->GetBestSize());
  cancelBtn->SetMinSize(cancelBtn->GetBestSize());

  int gap = std::max(6, PAD / 2);
  bottomSizer->Add(applyBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
  bottomSizer->Add(okBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
  bottomSizer->Add(cancelBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, PAD);

  // Add scrolled and bottom sizer to main
  mainSizer->Add(scrolled, 1, wxEXPAND | wxALL, PAD);
  mainSizer->Add(bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, PAD);

  // Apply sizer to dialog
  SetSizer(mainSizer);
  mainSizer->SetSizeHints(this);

  // -------------------------
  // Correct sizing sequence to avoid premature horizontal scrollbar:
  // 1) compute desired width from buttons and a conservative content estimate
  // 2) cap to screen
  // 3) compute availW for Wrap()
  // 4) Wrap static texts
  // 5) Layout/CalcMin/FitInside and recompute desired sizes
  // -------------------------

  // 1) conservative content estimate: sum of child widths (CalcMin may be large
  // before wrap)
  wxSize contentEstimate = contentSizer->CalcMin();

  // 2) width required by buttons (so Cancel not clipped)
  int rightButtonsTotalW = applyBtn->GetBestSize().GetWidth() +
                           okBtn->GetBestSize().GetWidth() +
                           cancelBtn->GetBestSize().GetWidth();
  int computedBottomWidth = restoreBtn->GetBestSize().GetWidth() +
                            rightButtonsTotalW + 3 * PAD + 2 * gap;

  // 3) initial desired width is max of content estimate and buttons
  int desiredWidth = std::max(contentEstimate.GetWidth(), computedBottomWidth);

  // 4) cap to screen
  wxSize displaySize = wxGetDisplaySize();
  const int SCREEN_MARGIN = this->ConvertDialogToPixels(wxSize(10, 10)).y;
  int maxAllowedWidth = displaySize.GetWidth() - 2 * SCREEN_MARGIN;
  int maxAllowedHeight = displaySize.GetHeight() - 2 * SCREEN_MARGIN;
  desiredWidth = std::min(desiredWidth, maxAllowedWidth);

  // 5) compute available width inside scrolled for wrapping text
  //    subtract outer PAD and a safety margin for static box borders
  int availW = desiredWidth - 2 * PAD - 20;
  if (availW < 80)
    availW = 80;

  // 6) Wrap static texts to availW BEFORE final sizing
  if (thumbHint)
    thumbHint->Wrap(availW);
  if (cacheNote)
    cacheNote->Wrap(availW);
  if (logoNote)
    logoNote->Wrap(availW);
  if (plNote)
    plNote->Wrap(availW);

  // 7) Re-layout and recompute content minimal size after wrapping
  contentSizer->Layout();
  scrolled->FitInside();
  wxSize contentMin = contentSizer->CalcMin();

  // 8) Recompute desired width (wrapping may have changed content width)
  desiredWidth = std::max(desiredWidth, contentMin.GetWidth());
  desiredWidth = std::min(desiredWidth, maxAllowedWidth);

  // 9) Compute desired height and cap to screen; enable vertical scroll if
  // needed
  wxSize bottomMin = bottomSizer->CalcMin();
  int desiredHeight = contentMin.GetHeight() + bottomMin.GetHeight() + 3 * PAD;
  if (desiredHeight > maxAllowedHeight) {
    // content taller than screen: limit dialog height and let scrolled show
    // vertical scrollbar
    desiredHeight = maxAllowedHeight;
    int scrolledH = maxAllowedHeight - bottomMin.GetHeight() - 3 * PAD;
    if (scrolledH < 80)
      scrolledH = 80;
    scrolled->SetMinSize(wxSize(-1, scrolledH));
  } else {
    scrolled->SetMinSize(wxSize(-1, contentMin.GetHeight()));
  }

  // 10) Ensure dialog cannot be shrunk below buttons width
  int minWidth = std::min(maxAllowedWidth, computedBottomWidth);
  int minHeight =
      std::min(maxAllowedHeight, this->ConvertDialogToPixels(wxSize(0, 200)).y);
  this->SetMinSize(wxSize(minWidth, minHeight));

  // 11) Set final size and layout
  this->SetSize(wxSize(desiredWidth, desiredHeight));
  Layout();
  scrolled->FitInside();
  scrolled->SetScrollRate(5, 5);

  // Center on parent
  CentreOnParent();
  LoadEPGSettings();

  // ---------------- Bind handlers ----------------
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

  // Bind EPG handlers
  m_epgAddSource->Bind(wxEVT_BUTTON, &SettingsDialog::OnEPGAddSource, this);
  m_epgEditSource->Bind(wxEVT_BUTTON, &SettingsDialog::OnEPGEditSource, this);
  m_epgRemoveSource->Bind(wxEVT_BUTTON, &SettingsDialog::OnEPGRemoveSource,
                          this);
  m_epgRefreshNow->Bind(wxEVT_BUTTON, &SettingsDialog::OnEPGRefreshNow, this);
  m_epgDeleteCache->Bind(wxEVT_BUTTON, &SettingsDialog::OnEPGDeleteCache, this);
}

// ----------------- Apply / OK / Cancel / Restore -----------------
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

  if (m_epgAutoUpdate)
    m_epgAutoUpdate->SetValue(false);
  if (m_epgUpdateInterval)
    m_epgUpdateInterval->SetValue(24);
  if (m_epgDaysToKeep)
    m_epgDaysToKeep->SetValue(3);
}

void SettingsDialog::OnRestoreDefaults(wxCommandEvent &WXUNUSED(event)) {
  RestoreDefaults();
}

void SettingsDialog::ApplySettings(bool saveConfig) {
  if (!m_mainFrame)
    return;

  bool show = m_noLogoCheck ? m_noLogoCheck->GetValue() : false;

  // Apply to main frame
  m_mainFrame->SetShowLogoFromSettings(show);

  // Если пользователь выключил логотипы и отмечен чекбокс удаления кеша
  if (!show && m_deleteLogoCacheOnDisableCheck &&
      m_deleteLogoCacheOnDisableCheck->GetValue()) {
    MainFrame *mf = m_mainFrame;

    std::thread([mf, this]() {
      // Выполняем очистку (может использовать this внутри синхронной функции,
      // но результат обработки UI не должен ссылаться на this)
      ErrorCode ec = this->ClearAllCachesSync();
      // В CallAfter используем только mf и ec — НЕ this
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

  SaveEPGSettings();
}

// ----------------- Split cache handlers -----------------

void SettingsDialog::OnDeleteDiskCacheNow(wxCommandEvent &WXUNUSED(event)) {
  // Подтверждение удаления дискового кеша
  wxMessageDialog dlg(this, "Delete on-disk logo cache (icon files) from disk?",
                      "Confirm delete disk cache", wxYES_NO | wxICON_WARNING);
  if (dlg.ShowModal() != wxID_YES) {
    wxLogDebug("OnDeleteDiskCacheNow: user cancelled disk cache deletion");
    return;
  }

  if (m_mainFrame) {
    m_mainFrame->SetStatusText("Deleting on-disk logo cache...", 0);
  }

  // Фоновая задача: удаляем файлы и сообщаем результат
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
        // Для модального окна используйте mf как parent
        // wxMessageBox("Failed to delete disk logo files. See log for
        // details.",
        //           "Error", wxOK | wxICON_ERROR, mf);
      }
    });
  }).detach();
}

void SettingsDialog::OnDeleteRamCacheNow(wxCommandEvent &WXUNUSED(event)) {
  // Подтверждение очистки RAM-кеша
  wxMessageDialog dlg(this, "Clear in-memory logo cache (RAM)?",
                      "Confirm clear RAM cache", wxYES_NO | wxICON_QUESTION);
  if (dlg.ShowModal() != wxID_YES) {
    wxLogDebug("OnDeleteRamCacheNow: user cancelled RAM cache clear");
    return;
  }

  // Очистка in-memory сразу (операция быстрая)
  LogoCache::ClearAll();
  wxLogInfo("OnDeleteRamCacheNow: in-memory logo cache cleared.");

  if (m_mainFrame) {
    m_mainFrame->SetStatusText("In-memory logo cache cleared.", 0);
  } else {
    // wxMessageBox("In-memory logo cache cleared.", "Clear RAM cache",
    //            wxOK | wxICON_INFORMATION, this);
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

// Выполняет полную очистку (RAM + диск) синхронно и возвращает код ошибки.
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

  // 4) При успешном удалении диска — очистим глобальный in-memory LogoCache
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

  // 5) Форсированная инвалидация UI и возобновление загрузок
  if (channelCards) {
    channelCards->InvalidateAll();
    channelCards->ResumeLogoLoading();
  }

  return ec;
}

// ---------------------------------------------------------------------
// EPG methods
// ---------------------------------------------------------------------
void SettingsDialog::LoadEPGSettings() {
  Application *app = static_cast<Application *>(wxTheApp);
  if (!app)
    return;
  auto *epgMgr = app->GetEPGManager();
  if (!epgMgr)
    return;

  m_epgAutoUpdate->SetValue(epgMgr->IsAutoUpdateEnabled());
  m_epgUpdateInterval->SetValue(epgMgr->GetUpdateIntervalHours());
  m_epgDaysToKeep->SetValue(epgMgr->GetDaysToKeep());

  UpdateEPGSourceList();
}

void SettingsDialog::SaveEPGSettings() {
  Application *app = static_cast<Application *>(wxTheApp);
  if (!app)
    return;

  auto *epgMgr = app->GetEPGManager();
  if (!epgMgr)
    return;

  epgMgr->SetAutoUpdateEnabled(m_epgAutoUpdate->GetValue());
  epgMgr->SetUpdateIntervalHours(m_epgUpdateInterval->GetValue());
  epgMgr->SetDaysToKeep(m_epgDaysToKeep->GetValue());

  app->RestartEpgTimer();
}

void SettingsDialog::UpdateEPGSourceList() {
  Application *app = static_cast<Application *>(wxTheApp);
  if (!app)
    return;
  auto *epgMgr = app->GetEPGManager();
  if (!epgMgr)
    return;

  auto sources = epgMgr->GetSources();
  m_epgSourceList->DeleteAllItems();
  for (size_t i = 0; i < sources.size(); ++i) {
    long idx =
        m_epgSourceList->InsertItem(i, wxString::FromUTF8(sources[i].url));
    m_epgSourceList->SetItem(idx, 1, wxString::FromUTF8(sources[i].name));
    m_epgSourceList->SetItem(idx, 2,
                             EpgTime::FormatTime(sources[i].lastUpdate));
  }
}

void SettingsDialog::OnEPGAddSource(wxCommandEvent &) {
  wxString urlOrPath, name;
  bool isFile;
  int result = ShowAddEpgSourceDialog(this, urlOrPath, name, isFile);

  if (result == -1) {
    return; // отмена
  }

  if (result == 0) {
    UpdateEPGSourceList();
    wxMessageBox("EPG source added successfully.", "Success",
                 wxOK | wxICON_INFORMATION);
  } else if (result == 1) {
    wxMessageBox("This EPG source already exists.", "Info",
                 wxOK | wxICON_INFORMATION);
  } else {
    wxMessageBox("Failed to add EPG source.", "Error", wxOK | wxICON_ERROR);
  }
}

int SettingsDialog::AddEpgSourceToManager(const wxString &urlOrPath,
                                          const wxString &name, bool isFile) {
  Application *app = static_cast<Application *>(wxTheApp);
  if (!app)
    return 2;
  auto *epgMgr = app->GetEPGManager();
  if (!epgMgr)
    return 2;

  wxString finalName = name;
  if (finalName.IsEmpty()) {
    if (isFile) {
      wxFileName fn(urlOrPath);
      finalName = fn.GetName();
    } else {
      wxString path = urlOrPath.AfterLast('/').BeforeFirst('?');
      if (path.EndsWith(".xml") || path.EndsWith(".gz") ||
          path.EndsWith(".xml.gz")) {
        path = path.BeforeLast('.');
      }
      finalName =
          path.IsEmpty() ? urlOrPath.BeforeFirst('/').AfterFirst('/') : path;
    }
  }

  auto sources = epgMgr->GetSources();
  std::string urlUtf8 = urlOrPath.ToUTF8().data();
  for (const auto &src : sources) {
    if (src.url == urlUtf8) {
      return 1; // дубликат
    }
  }

  EpgSource src;
  src.url = urlUtf8;
  src.name = finalName.ToUTF8().data();
  src.lastUpdate = 0;
  sources.push_back(src);
  epgMgr->SetSources(sources);
  epgMgr->SaveSourcesToConfig();
  epgMgr->Refresh(); // асинхронная загрузка

  return 0; // успех
}

int SettingsDialog::ShowAddEpgSourceDialog(wxWindow *parent,
                                           wxString &urlOrPath, wxString &name,
                                           bool &isFile) {
  AddEpgSourceDialog dlg(parent);
  if (dlg.ShowModal() != wxID_OK) {
    return -1; // отмена
  }

  urlOrPath = dlg.GetUrlOrPath();
  name = dlg.GetName();
  isFile = dlg.IsFileSource();

  return AddEpgSourceToManager(urlOrPath, name, isFile);
}

void SettingsDialog::OnEPGEditSource(wxCommandEvent &) {
  long sel =
      m_epgSourceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel == -1) {
    wxMessageBox("Please select a source to edit.", "Info",
                 wxOK | wxICON_INFORMATION);
    return;
  }

  Application *app = static_cast<Application *>(wxTheApp);
  if (!app)
    return;
  auto *epgMgr = app->GetEPGManager();
  if (!epgMgr)
    return;

  auto sources = epgMgr->GetSources();
  if (sel >= (long)sources.size())
    return;

  wxString oldUrl = wxString::FromUTF8(sources[sel].url);
  wxString oldName = wxString::FromUTF8(sources[sel].name);

  wxTextEntryDialog urlDlg(this, "Edit EPG source URL:", "Edit EPG Source",
                           oldUrl);
  if (urlDlg.ShowModal() != wxID_OK)
    return;
  wxString newUrl = urlDlg.GetValue();

  wxTextEntryDialog nameDlg(this, "Edit source name:", "Edit Source Name",
                            oldName);
  wxString newName;
  if (nameDlg.ShowModal() == wxID_OK) {
    newName = nameDlg.GetValue();
  }

  sources[sel].url = newUrl.ToUTF8().data();
  sources[sel].name = newName.ToUTF8().data();
  epgMgr->SetSources(sources);

  UpdateEPGSourceList();
  epgMgr->SaveSourcesToConfig();
}

void SettingsDialog::OnEPGRemoveSource(wxCommandEvent &) {
  long sel =
      m_epgSourceList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel == -1) {
    wxMessageBox("Please select a source to remove.", "Info",
                 wxOK | wxICON_INFORMATION);
    return;
  }

  if (wxMessageBox("Remove selected EPG source?", "Confirm",
                   wxYES_NO | wxICON_QUESTION) != wxYES)
    return;

  Application *app = static_cast<Application *>(wxTheApp);
  if (!app)
    return;
  auto *epgMgr = app->GetEPGManager();
  if (!epgMgr)
    return;

  auto sources = epgMgr->GetSources();
  if (sel < (long)sources.size()) {
    sources.erase(sources.begin() + sel);
    epgMgr->SetSources(sources);
    UpdateEPGSourceList();
    epgMgr->SaveSourcesToConfig();
  }
}

void SettingsDialog::OnEPGRefreshNow(wxCommandEvent &) {
  Application *app = static_cast<Application *>(wxTheApp);
  if (!app)
    return;
  auto *epgMgr = app->GetEPGManager();
  if (!epgMgr)
    return;

  epgMgr->Refresh();
  wxMessageBox("EPG refresh started in background.", "Info",
               wxOK | wxICON_INFORMATION);
}

void SettingsDialog::OnEPGDeleteCache(wxCommandEvent &) {
  Application *app = static_cast<Application *>(wxTheApp);
  if (!app)
    return;
  auto *epgMgr = app->GetEPGManager();
  if (!epgMgr)
    return;

  if (wxMessageBox("Delete all locally cached EPG data? This will force "
                   "re-download on next update.",
                   "Confirm", wxYES_NO | wxICON_QUESTION) != wxYES) {
    return;
  }

  // Удаляем файл кэша
  if (epgMgr->DeleteCache()) {
    wxMessageBox("EPG cache deleted.", "Info", wxOK | wxICON_INFORMATION);
    if (m_mainFrame) {
      m_mainFrame->SetStatusText("EPG cache deleted.", 0);
    }
  } else {
    wxMessageBox("Failed to delete EPG cache.", "Error", wxOK | wxICON_ERROR);
  }
}


