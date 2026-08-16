#pragma once

#include "ErrorCode.h"

#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/wx.h>

class MainFrame;
class ConfigManager;
class EpgSourceManagerPanel;

class SettingsDialog : public wxDialog {
public:
  SettingsDialog(MainFrame *parent, ConfigManager *cfg);

private:
  // Контролы
  wxCheckBox *m_noLogoCheck = nullptr;
  wxCheckBox *m_autoUpdateCheck = nullptr;
  wxCheckBox *m_deleteLogoCacheOnDisableCheck = nullptr;
  wxButton *m_deleteDiskCacheBtn = nullptr;
  wxButton *m_deleteRamCacheBtn = nullptr;
  wxButton *m_reloadMissingLogosBtn = nullptr;

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

  void OnDeleteDiskCacheNow(wxCommandEvent &WXUNUSED(event));
  void OnDeleteRamCacheNow(wxCommandEvent &WXUNUSED(event));
  void OnReloadMissingLogos(wxCommandEvent &event);

  ErrorCode ClearAllCachesSync();
};
