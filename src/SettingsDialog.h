#pragma once

#include "ErrorCode.h"

#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/wx.h>

class MainFrame;
class ConfigManager;

class SettingsDialog : public wxDialog {
public:
  SettingsDialog(MainFrame *parent, ConfigManager *cfg);

private:
  // EPG controls
  wxCheckBox *m_epgAutoUpdate = nullptr;
  wxSpinCtrl *m_epgUpdateInterval = nullptr;
  wxSpinCtrl *m_epgDaysToKeep = nullptr;
  wxListCtrl *m_epgSourceList = nullptr;
  wxButton *m_epgAddSource = nullptr;
  wxButton *m_epgEditSource = nullptr;
  wxButton *m_epgRemoveSource = nullptr;
  wxButton *m_epgRefreshNow = nullptr;
  wxButton *m_epgDeleteCache = nullptr;

  void OnEPGAddSource(wxCommandEvent &event);
  void OnEPGEditSource(wxCommandEvent &event);
  void OnEPGRemoveSource(wxCommandEvent &event);
  void OnEPGRefreshNow(wxCommandEvent &event);
  void OnEPGDeleteCache(wxCommandEvent &event);
  void UpdateEPGSourceList();
  void LoadEPGSettings();
  void SaveEPGSettings();

  // Контролы
  wxCheckBox *m_noLogoCheck = nullptr;
  wxCheckBox *m_autoUpdateCheck = nullptr;

  // Ссылки
  ConfigManager *m_config = nullptr;
  MainFrame *m_mainFrame = nullptr;

  // Методы
  void ApplySettings(bool saveConfig);
  void RestoreDefaults();

  // Обработчики
  void OnApply(wxCommandEvent &event);
  void OnOk(wxCommandEvent &event);
  void OnCancel(wxCommandEvent &event);
  void OnRestoreDefaults(wxCommandEvent &event);
  
  wxCheckBox *m_deleteLogoCacheOnDisableCheck = nullptr;
  wxButton *m_deleteDiskCacheBtn = nullptr;
  wxButton *m_deleteRamCacheBtn = nullptr;
  wxButton *m_reloadMissingLogosBtn = nullptr;

  void OnDeleteDiskCacheNow(wxCommandEvent &WXUNUSED(event));
  void OnDeleteRamCacheNow(wxCommandEvent &WXUNUSED(event));
  void OnReloadMissingLogos(wxCommandEvent &event);

  ErrorCode ClearAllCachesSync();
};
