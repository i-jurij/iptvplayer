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
  // Показывает диалог и возвращает данные через ссылки.
  // Возвращает: 0 – успех, 1 – дубликат, 2 – ошибка (или отмена – тогда
  // возвращает -1)
  static int ShowAddEpgSourceDialog(wxWindow *parent, wxString &urlOrPath,
                                    wxString &name, bool &isFile);

  // Добавляет EPG-источник в менеджер (проверка дубликата, сохранение, запуск
  // Refresh) Возвращает: 0 – успех, 1 – дубликат, 2 – ошибка (app или epgMgr ==
  // nullptr)
  static int AddEpgSourceToManager(const wxString &urlOrPath,
                                   const wxString &name, bool isFile);

private:
  // Контролы
  wxCheckBox *m_noLogoCheck = nullptr;
  wxCheckBox *m_autoUpdateCheck = nullptr;
  wxCheckBox *m_deleteLogoCacheOnDisableCheck = nullptr;
  wxButton *m_deleteDiskCacheBtn = nullptr;
  wxButton *m_deleteRamCacheBtn = nullptr;
  wxButton *m_reloadMissingLogosBtn = nullptr;

  // Панель управления EPG
  EpgSourceManagerPanel *m_epgPanel = nullptr;

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
